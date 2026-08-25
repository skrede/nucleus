// Lexical and structural robustness of the xml source, and the boundary checks
// that must run before a document is read: input that is hostile, malformed, or
// merely unusual must produce loud, accurate errors -- or resolve correctly when
// the document is valid but exotic (CDATA values).

#include "chain_admissibility_test_support.h"

#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <optional>

using nucleus::anchor;

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

}

TEST_CASE("a pathologically deep document fails with a depth-cap error, not a crash",
          "[xml][robustness]")
{
    constexpr int levels = 100;
    std::string doc;
    for(int i = 0; i < levels; ++i)
        doc += "<a>";
    doc += "v";
    for(int i = 0; i < levels; ++i)
        doc += "</a>";

    auto src = xml_of(doc);
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("depth cap") != std::string::npos);
}

TEST_CASE("a transparent root's children start one level below the root for the "
          "depth cap", "[xml][robustness]")
{
    // The named-space root contributes no path segment, so its children are the
    // first level the cap counts. Pinning both sides of the boundary keeps the
    // count from drifting by one when the root's own handling changes.
    const auto nested = [](int levels) {
        std::string doc = "<engine>";
        for(int i = 0; i < levels; ++i)
            doc += "<a>";
        doc += "v";
        for(int i = 0; i < levels; ++i)
            doc += "</a>";
        return doc + "</engine>";
    };

    auto under = xml_of(nested(65));
    REQUIRE(under.with_space_name("engine").pull());

    auto over = xml_of(nested(66));
    auto pulled = over.with_space_name("engine").pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("depth cap") != std::string::npos);
}

TEST_CASE("a CDATA leaf value resolves like plain text", "[xml][robustness]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("motd", anchor::keyspace("server"))));
    const nucleus::config_space space = nucleus::builder_result_test::built(builder);

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{xml_of(
            "<server><motd><![CDATA[a <b> & c]]></motd></server>")},
        {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("server/motd") == "a <b> & c");
}

TEST_CASE("a CDATA primary-key child element names its strain", "[xml][robustness]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::element("port", anchor::keyspace("cluster/server"))));
    const nucleus::config_space space = nucleus::builder_result_test::built(builder);

    nucleus::load_options options;
    options.selection = "web";
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{xml_of(
            "<cluster><server><name><![CDATA[web]]></name><port>80</port></server>"
            "</cluster>")},
        options);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("cluster/server/port") == "80");
}

TEST_CASE("a nonexistent file reports an unreadable file, not a parse failure",
          "[xml][robustness]")
{
    auto src = nucleus::xml_source::from(
        nucleus::xml_source_options::of_file("no/such/directory/config.xml"));
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("cannot read file") != std::string::npos);
    REQUIRE(pulled.error().message.find("no/such/directory/config.xml") != std::string::npos);
    REQUIRE(pulled.error().message.find("parse") == std::string::npos);
}

TEST_CASE("garbage input reports a parse failure with an offset", "[xml][robustness]")
{
    auto src = xml_of("\x01\x02 this is not xml at all >>>");
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("failed to parse input") != std::string::npos);
    REQUIRE(pulled.error().message.find("offset") != std::string::npos);
}

TEST_CASE("an element-free document reports the missing document element",
          "[xml][robustness]")
{
    // pugixml itself rejects a document with no element at parse time, so this
    // surfaces through the parse-failure branch with pugixml's description.
    auto src = xml_of("<!-- only a comment, no root -->");
    auto pulled = src.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().message.find("document element") != std::string::npos);
}

TEST_CASE("a parent the policy refuses is never read, and neither is its own parent",
          "[xml][robustness]")
{
    namespace probe = nucleus::chain_admissibility_test;

    const nucleus::config_space space = probe::chain_space();
    auto log = std::make_shared<probe::probe_log>();

    nucleus::load_options options;
    options.document_paths        = {"child.xml"};
    options.make_document         = probe::probe_factory(probe::refused_middle_chain(), log);
    options.selection             = "web";
    options.inherit.admissibility = probe::reject_without_nesting();

    auto loaded = nucleus::load_config(space, nucleus::source_stack{}, options);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().message.find("parent.xml") != std::string::npos);

    REQUIRE(log->build_count["parent.xml"] == 1);
    REQUIRE(log->pull_count["parent.xml"] == 0);
    REQUIRE(log->build_count["grandparent.xml"] == 0);
    REQUIRE(log->pull_count["grandparent.xml"] == 0);

    // The requested-set exemption and the single-visit short-circuit both survive
    // the reordering: parent.xml is now requested directly as well as reached.
    auto exempt_log = std::make_shared<probe::probe_log>();
    options.document_paths = {"child.xml", "parent.xml"};
    options.make_document  = probe::probe_factory(probe::refused_middle_chain(), exempt_log);

    auto exempt = nucleus::load_config(space, nucleus::source_stack{}, options);
    REQUIRE(exempt);
    REQUIRE(exempt_log->pull_count["parent.xml"] == 1);
}

TEST_CASE("an empty inherit attribute is a malformed declaration, not a parent path",
          "[xml][robustness]")
{
    auto empty = xml_of(R"(<cluster inherit=""><server><port>80</port></server></cluster>)");
    auto pulled = empty.pull();
    REQUIRE_FALSE(pulled);
    REQUIRE(pulled.error().code == nucleus::errc::malformed_source);
    REQUIRE(pulled.error().message.find("cluster") != std::string::npos);

    // A named-space envelope validates its root attributes on a separate path.
    auto named_root = xml_of(R"(<engine inherit=""><port>80</port></engine>)");
    REQUIRE_FALSE(named_root.with_space_name("engine").pull());

    // The opt-out keyword and a named parent are unaffected; the chain suite pins
    // what each of the three legal inherit shapes then resolves to.
    auto opted_out = xml_of(R"(<cluster inherit="none"><server><port>80</port></server></cluster>)");
    REQUIRE(opted_out.pull());
    auto named = xml_of(R"(<cluster inherit="base.xml"><server><port>80</port></server></cluster>)");
    REQUIRE(named.pull());
}

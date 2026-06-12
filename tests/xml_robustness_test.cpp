// Lexical and structural robustness of the xml source: input that is hostile,
// malformed, or merely unusual must produce loud, accurate errors -- or resolve
// correctly when the document is valid but exotic (CDATA values).

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("a CDATA leaf value resolves like plain text", "[xml][robustness]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("motd", anchor::keyspace("server"))));
    const nucleus::config_space space = builder.build();

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
    const nucleus::config_space space = builder.build();

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

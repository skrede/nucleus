#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/provenance.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <utility>
#include <filesystem>
#include <string_view>

// Round-trip proof: a config loaded from XML, persisted via the stream-based
// emit_document inside the xml module, and re-loaded yields the same keys and values
// -- including repeated collections (no last-wins loss). A nested, primary-key-free
// schema keeps the resolved keyspace stable across the round-trip. The test owns
// persistence: it supplies the ostream (an ostringstream, then a file).

using nucleus::anchor;

namespace {

nucleus::config checked_config(std::map<std::string, std::string> values)
{
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

void declare_server(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(engine.register_element(nucleus::element("host", anchor::keyspace("server"))));
    REQUIRE(engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("server"))));
}

nucleus::load_options document_options(const std::string &xml)
{
    nucleus::load_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [xml](const std::string &) { return xml_of(xml); };
    return opts;
}

const char *const kDocument =
    "<server>\n"
    "  <host>localhost</host>\n"
    "  <tag>alpha</tag>\n"
    "  <tag>beta</tag>\n"
    "</server>\n";

}

TEST_CASE("a resolved config round-trips through XML persistence", "[persist]")
{
    nucleus::config_space_builder engine;
    declare_server(engine);
    nucleus::config_space space = engine.build();

    auto first = nucleus::load_config(space, nucleus::source_stack{}, document_options(kDocument));
    REQUIRE(first);
    const nucleus::config &original = first.value();

    std::ostringstream serialized;
    REQUIRE(nucleus::xml::emit_document(original, serialized));

    auto second = nucleus::load_config(space, nucleus::source_stack{}, document_options(serialized.str()));
    REQUIRE(second);
    const nucleus::config &reloaded = second.value();

    // Same keys, same scalar values, same repeated collections.
    REQUIRE(reloaded.keys() == original.keys());
    for(const std::string &key : original.keys())
        REQUIRE(reloaded.get_all(key) == original.get_all(key));

    // The repeated leaf persisted ALL its values, not just the last.
    REQUIRE(original.get_all("server/tag") == std::vector<std::string>{"alpha", "beta"});
    REQUIRE(reloaded.get_all("server/tag") == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("a duplicate attribute is rejected as malformed_source", "[persist][malformed]")
{
    nucleus::config_space_builder engine;
    declare_server(engine);
    nucleus::config_space space = engine.build();

    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options("<server host=\"a\" host=\"b\"><host>c</host></server>"));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    CHECK(loaded.error().message.find("host") != std::string::npos);
    CHECK(loaded.error().message.find("server") != std::string::npos);
}

TEST_CASE("more than one root element is rejected as malformed_source", "[persist][malformed]")
{
    nucleus::config_space_builder engine;
    declare_server(engine);
    nucleus::config_space space = engine.build();

    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options("<server><host>a</host></server>"
                         "<server><host>b</host></server>"));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    CHECK(loaded.error().message.find("more than one root") != std::string::npos);
}

TEST_CASE("trailing content after the root element is rejected as malformed_source",
          "[persist][malformed]")
{
    nucleus::config_space_builder engine;
    declare_server(engine);
    nucleus::config_space space = engine.build();

    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options("<server><host>a</host></server><![CDATA[trailing]]>"));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    CHECK(loaded.error().message.find("outside the root element") != std::string::npos);
}

TEST_CASE("content before the root element is rejected as malformed_source",
          "[persist][malformed]")
{
    nucleus::config_space_builder engine;
    declare_server(engine);
    nucleus::config_space space = engine.build();

    // Retained CDATA ahead of the root is as ill-formed as trailing content; the
    // sweep must catch it on both sides of the root, not just after.
    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options("<![CDATA[leading]]><server><host>a</host></server>"));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    CHECK(loaded.error().message.find("outside the root element") != std::string::npos);
}

TEST_CASE("an attribute-bearing text element is rejected as mixed content",
          "[persist][malformed]")
{
    nucleus::config_space_builder engine;
    declare_server(engine);
    nucleus::config_space space = engine.build();

    // <host attr="x">8080</host> carries both an attribute and text: reading the
    // attribute while dropping the text would be silent value loss, so it is
    // rejected as mixed content.
    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options("<server><host attr=\"x\">8080</host></server>"));
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().code == nucleus::errc::malformed_source);
    CHECK(loaded.error().message.find("mixes character data") != std::string::npos);
}

TEST_CASE("emit_document wraps a multi-root config so its own reader accepts it",
          "[persist][emit]")
{
    // A config with more than one top-level key segment is not a single-root XML
    // document. The emitter must wrap it (as emit_template does) rather than write
    // a multi-root document the hardened reader now refuses on re-read.
    std::map<std::string, std::string> values{
        {"alpha/x", "1"}, {"beta/y", "2"}};
    const nucleus::config cfg = checked_config(std::move(values));

    std::ostringstream out;
    REQUIRE(nucleus::xml::emit_document(cfg, out));

    nucleus::config_space_builder engine;
    REQUIRE(engine.register_schema("config/alpha/x"));
    REQUIRE(engine.register_schema("config/beta/y"));
    nucleus::config_space space = engine.build();

    auto reloaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options(out.str()));
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().get("config/alpha/x") == "1");
    REQUIRE(reloaded.value().get("config/beta/y") == "2");
}

TEST_CASE("emit_document + load round-trip preserves a bare single-root leaf",
          "[persist][emit][fidelity]")
{
    // A config with exactly one single-segment key emits as a bare single-root leaf
    // (<port>8080</port>, unwrapped). The unnamed read path must read the root's own
    // text -- the structural walk reads only attributes and leaf children, never the
    // walked node's own text -- or the value vanishes on reload.
    std::map<std::string, std::string> values{{"port", "8080"}};
    const nucleus::config cfg = checked_config(std::move(values));

    std::ostringstream out;
    REQUIRE(nucleus::xml::emit_document(cfg, out));

    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("port", anchor::root())));
    nucleus::config_space space = engine.build();

    auto reloaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options(out.str()));
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().get("port") == "8080");
}

TEST_CASE("a hand-written single-root leaf reads its own text on the unnamed path",
          "[persist][fidelity]")
{
    // A single-root leaf authored by hand (not via the emitter) reads its text too:
    // the reader honors the DOM text() of a bare root element.
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("port", anchor::root())));
    nucleus::config_space space = engine.build();

    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options("<port>8080</port>"));
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("port") == "8080");
}

TEST_CASE("emit_document wraps an empty config so its own reader accepts it",
          "[persist][emit]")
{
    // An empty config previously emitted only the XML declaration -- a rootless
    // document the reader rejects ("no root element"). emit_document must wrap zero
    // top-level elements in <config/>, symmetric with emit_template.
    const nucleus::config cfg;

    std::ostringstream out;
    REQUIRE(nucleus::xml::emit_document(cfg, out));

    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("config", anchor::root())));
    nucleus::config_space space = engine.build();

    auto reloaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options(out.str()));
    REQUIRE(reloaded);
}

TEST_CASE("emit_document + load round-trip preserves a whitespace-only value",
          "[persist][emit][fidelity]")
{
    // pugixml's default parse flags discard whitespace-only pcdata, so a plain-text
    // " " would reload as "". The emitter writes a whitespace-only value as a CDATA
    // section, which is retained verbatim and read back intact.
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("port", anchor::root())));
    nucleus::config_space space = engine.build();

    for(const std::string &ws : {std::string(" "), std::string("\t"), std::string("\n")})
    {
        std::map<std::string, std::string> values{{"port", ws}};
        const nucleus::config cfg = checked_config(std::move(values));

        std::ostringstream out;
        REQUIRE(nucleus::xml::emit_document(cfg, out));

        auto reloaded = nucleus::load_config(space, nucleus::source_stack{},
            document_options(out.str()));
        REQUIRE(reloaded);
        CHECK(reloaded.value().get("port") == ws);
    }
}

TEST_CASE("emit_document + load round-trip preserves a whitespace-only repeated-leaf value",
          "[persist][emit][fidelity]")
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("tags", anchor::keyspace("cluster"))));
    nucleus::config_space space = engine.build();

    std::map<std::string, std::string> values{
        {"cluster/tags[0]", " "}, {"cluster/tags[1]", "x"}};
    const nucleus::config cfg = checked_config(std::move(values));

    std::ostringstream out;
    REQUIRE(nucleus::xml::emit_document(cfg, out));

    auto reloaded = nucleus::load_config(space, nucleus::source_stack{},
        document_options(out.str()));
    REQUIRE(reloaded);
    REQUIRE(reloaded.value().get_all("cluster/tags")
            == std::vector<std::string>{" ", "x"});
}

TEST_CASE("a whitespace-only value round-trips on the named-space path",
          "[persist][emit][fidelity]")
{
    std::map<std::string, std::string> values{{"motd", " "}};
    const nucleus::config cfg = checked_config(std::move(values));

    std::ostringstream out;
    REQUIRE(nucleus::xml::emit_document(cfg, out, std::string_view("server")));

    auto src = nucleus::xml_source::from(
        nucleus::xml_source_options::of_string(out.str()));
    src.with_space_name("server");
    auto result = src.pull();
    REQUIRE(result);

    bool found = false;
    for(const auto &e : result.value().entries)
        if(e.path == "motd")
        {
            CHECK(std::string(e.value.text()) == " ");
            found = true;
        }
    CHECK(found);
}

TEST_CASE("emit_document to a file persists a config that re-reads identically", "[persist]")
{
    nucleus::config_space_builder engine;
    declare_server(engine);
    nucleus::config_space space = engine.build();

    auto first = nucleus::load_config(space, nucleus::source_stack{}, document_options(kDocument));
    REQUIRE(first);

    // The test owns persistence: emit into a file stream it opens, then re-read it.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "nucleus_xml_persist_test.xml";
    {
        std::ofstream out(path);
        REQUIRE(nucleus::xml::emit_document(first.value(), out));
    }

    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();
    std::filesystem::remove(path);

    auto second = nucleus::load_config(space, nucleus::source_stack{}, document_options(buffer.str()));
    REQUIRE(second);
    REQUIRE(second.value().keys() == first.value().keys());
    REQUIRE(second.value().get_all("server/tag")
            == std::vector<std::string>{"alpha", "beta"});
}

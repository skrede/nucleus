#include "nucleus/configuration_space.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <memory>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>

// Round-trip proof: a configuration loaded from XML, persisted via the stream-based
// emit_document inside the xml module, and re-loaded yields the same keys and values
// -- including repeated collections (no last-wins loss). A nested, primary-key-free
// schema keeps the resolved keyspace stable across the round-trip. The test owns
// persistence: it supplies the ostream (an ostringstream, then a file).

using nucleus::anchor;

namespace {

std::unique_ptr<nucleus::configuration_source> xml_of(const std::string &text)
{
    return std::make_unique<nucleus::xml::xml_source>(
        nucleus::xml::xml_source::from(nucleus::xml::xml_source_options::of_string(text)));
}

void declare_server(nucleus::configuration_space_builder &engine)
{
    engine.register_element(nucleus::element("server", anchor::root()));
    engine.register_element(nucleus::element("host", anchor::keyspace("server")));
    engine.register_element(nucleus::repeated_element("tag", anchor::keyspace("server")));
}

nucleus::source_stack_options document_options(const std::string &xml)
{
    nucleus::source_stack_options opts;
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

TEST_CASE("a resolved configuration round-trips through XML persistence", "[persist]")
{
    nucleus::configuration_space_builder engine;
    declare_server(engine);
    nucleus::configuration_space space = engine.build();

    auto first = nucleus::load_configuration(space, document_options(kDocument));
    REQUIRE(first);
    const nucleus::configuration &original = first.value();

    std::ostringstream serialized;
    nucleus::xml::emit_document(original, serialized);

    auto second = nucleus::load_configuration(space, document_options(serialized.str()));
    REQUIRE(second);
    const nucleus::configuration &reloaded = second.value();

    // Same keys, same scalar values, same repeated collections.
    REQUIRE(reloaded.keys() == original.keys());
    for(const std::string &key : original.keys())
        REQUIRE(reloaded.get_all(key) == original.get_all(key));

    // The repeated leaf persisted ALL its values, not just the last.
    REQUIRE(original.get_all("server/tag") == std::vector<std::string>{"alpha", "beta"});
    REQUIRE(reloaded.get_all("server/tag") == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("emit_document to a file persists a configuration that re-reads identically", "[persist]")
{
    nucleus::configuration_space_builder engine;
    declare_server(engine);
    nucleus::configuration_space space = engine.build();

    auto first = nucleus::load_configuration(space, document_options(kDocument));
    REQUIRE(first);

    // The test owns persistence: emit into a file stream it opens, then re-read it.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "nucleus_xml_persist_test.xml";
    {
        std::ofstream out(path);
        nucleus::xml::emit_document(first.value(), out);
    }

    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();
    std::filesystem::remove(path);

    auto second = nucleus::load_configuration(space, document_options(buffer.str()));
    REQUIRE(second);
    REQUIRE(second.value().keys() == first.value().keys());
    REQUIRE(second.value().get_all("server/tag")
            == std::vector<std::string>{"alpha", "beta"});
}

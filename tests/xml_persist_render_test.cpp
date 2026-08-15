#include "xml_persist_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <string_view>

namespace test = nucleus::xml_persist_test;

namespace {

std::string read_and_remove(const std::filesystem::path &path)
{
    std::ifstream     input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    input.close();
    std::filesystem::remove(path);
    return buffer.str();
}

}

TEST_CASE("emit_document wraps a multi-root config so its own reader accepts it",
          "[persist][emit]")
{
    const nucleus::config config = test::checked_config(
            {{"alpha/x", "1"}, {"beta/y", "2"}});
    const auto rendered = nucleus::xml::render_document_schema_blind(config);
    REQUIRE(rendered);

    nucleus::config_space_builder builder;
    REQUIRE(builder.register_schema("config/alpha/x"));
    REQUIRE(builder.register_schema("config/beta/y"));
    const nucleus::config_space space    = builder.build();
    const auto                  reloaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(rendered.value()));
    REQUIRE(reloaded);
    REQUIRE(reloaded->get("config/alpha/x") == "1");
    REQUIRE(reloaded->get("config/beta/y") == "2");
}

TEST_CASE("emit_document + load round-trip preserves a bare single-root leaf",
          "[persist][emit][fidelity]")
{
    const nucleus::config_space space    = test::port_space();
    const nucleus::config       config   = test::checked_config({{"port", "8080"}});
    const auto                  rendered = nucleus::xml::render_document(config, space);
    REQUIRE(rendered);

    const auto reloaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(rendered.value()));
    REQUIRE(reloaded);
    REQUIRE(reloaded->get("port") == "8080");
}

TEST_CASE("emit_document wraps an empty config so its own reader accepts it",
          "[persist][emit]")
{
    const nucleus::config         config;
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("config", nucleus::anchor::root())));
    const nucleus::config_space space    = builder.build();
    const auto                  rendered = nucleus::xml::render_document(config, space);
    REQUIRE(rendered);

    const auto reloaded = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(rendered.value()));
    REQUIRE(reloaded);
}

TEST_CASE("a whitespace-only value round-trips on the named-space path",
          "[persist][emit][fidelity]")
{
    const nucleus::config config   = test::checked_config({{"motd", " "}});
    const auto            rendered = nucleus::xml::render_document_schema_blind(
            config, std::string_view("server"));
    REQUIRE(rendered);

    auto source = test::xml_of(rendered.value());
    source.with_space_name("server");
    const auto result = source.pull();
    REQUIRE(result);

    bool found = false;
    for(const auto &entry : result->entries)
        if(entry.path == "motd")
        {
            CHECK(std::string(entry.value.text()) == " ");
            found = true;
        }
    CHECK(found);
}

TEST_CASE("emit_document to a file persists a config that re-reads identically",
          "[persist]")
{
    const nucleus::config_space space = test::server_space();
    const auto                  first = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(test::server_document));
    REQUIRE(first);

    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "nucleus_xml_persist_test.xml";
    {
        std::ofstream output(path);
        REQUIRE(nucleus::xml::emit_document(first.value(), space, output));
    }

    const auto second = nucleus::load_config(
            space, nucleus::source_stack{},
            test::document_options(read_and_remove(path)));
    REQUIRE(second);
    REQUIRE(second->keys() == first->keys());
    REQUIRE(second->get_all("server/tag") == std::vector<std::string>{"alpha", "beta"});
}

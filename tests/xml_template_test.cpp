#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// emit_xml_template projects a sealed schema into a well-formed XML TEMPLATE:
// one element per declared field, nested by anchor path, with constrained fields
// annotated by their allowed values. Assertions are on structure/substrings, not
// byte-exact output, to stay robust to indentation.

namespace {

[[nodiscard]] nucleus::configuration_space make_server_space()
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server")));
    builder.register_element(
        nucleus::element("host", nucleus::anchor::keyspace("server")));
    builder.register_element(nucleus::enum_element(
        "mode", nucleus::anchor::keyspace("server"),
        std::vector<std::string>{"primary", "secondary"}));
    return builder.build();
}

}

TEST_CASE("emit_xml_template nests declared fields under their anchor element", "[template]")
{
    nucleus::configuration_space space = make_server_space();
    const std::string xml = space.emit_xml_template();

    // The container element is present and closed (well-formed nesting).
    const std::size_t server_open = xml.find("<server>");
    const std::size_t server_close = xml.find("</server>");
    REQUIRE(server_open != std::string::npos);
    REQUIRE(server_close != std::string::npos);

    // A nested leaf is emitted INSIDE the server element, not flattened.
    const std::size_t host = xml.find("<host");
    REQUIRE(host != std::string::npos);
    REQUIRE(host > server_open);
    REQUIRE(host < server_close);
}

TEST_CASE("emit_xml_template annotates a constrained field with its allowed values", "[template]")
{
    nucleus::configuration_space space = make_server_space();
    const std::string xml = space.emit_xml_template();

    // The constrained field carries its allowed-value annotation.
    const std::size_t mode = xml.find("<mode");
    REQUIRE(mode != std::string::npos);
    REQUIRE(xml.find("allowed=", mode) != std::string::npos);
    REQUIRE(xml.find("primary") != std::string::npos);
    REQUIRE(xml.find("secondary") != std::string::npos);
}

TEST_CASE("emit_xml_template leaves an unconstrained field unannotated", "[template]")
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::element("host", nucleus::anchor::keyspace("server")));
    nucleus::configuration_space space = builder.build();

    const std::string xml = space.emit_xml_template();
    REQUIRE(xml.find("allowed=") == std::string::npos);
}

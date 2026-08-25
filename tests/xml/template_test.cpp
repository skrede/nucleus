#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <sstream>

namespace {

std::string template_of(const nucleus::config_space &space)
{
    const auto rendered = nucleus::xml::render_template(space);
    REQUIRE(rendered);
    std::ostringstream delivered;
    REQUIRE(nucleus::xml::emit_template(space, delivered));
    REQUIRE(delivered.str() == rendered.value());
    return rendered.value();
}

}

namespace {

nucleus::config_space make_server_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(
            nucleus::element("host", nucleus::anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::enum_element(
            "mode", nucleus::anchor::keyspace("server"),
            std::vector<std::string>{"primary", "secondary"})));
    return nucleus::builder_result_test::built(builder);
}

}

TEST_CASE("emit_template nests declared fields under their anchor element", "[template]")
{
    nucleus::config_space space = make_server_space();
    const std::string     xml   = template_of(space);

    const std::size_t server_open  = xml.find("<server>");
    const std::size_t server_close = xml.find("</server>");
    REQUIRE(server_open != std::string::npos);
    REQUIRE(server_close != std::string::npos);

    const std::size_t host = xml.find("<host");
    REQUIRE(host != std::string::npos);
    REQUIRE(host > server_open);
    REQUIRE(host < server_close);
}

TEST_CASE("emit_template annotates a constrained field with its allowed values", "[template]")
{
    nucleus::config_space space = make_server_space();
    const std::string     xml   = template_of(space);

    const std::size_t mode = xml.find("<mode");
    REQUIRE(mode != std::string::npos);
    REQUIRE(xml.find("allowed=", mode) != std::string::npos);
    REQUIRE(xml.find("primary") != std::string::npos);
    REQUIRE(xml.find("secondary") != std::string::npos);
}

TEST_CASE("emit_template leaves an unconstrained field unannotated", "[template]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::element("host", nucleus::anchor::keyspace("server"))));
    nucleus::config_space space = nucleus::builder_result_test::built(builder);

    const std::string xml = template_of(space);
    REQUIRE(xml.find("allowed=") == std::string::npos);
}

#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>

namespace {

nucleus::config config_of(std::string key, std::string value)
{
    auto made = nucleus::config::from_values({{std::move(key), std::move(value)}});
    REQUIRE(made);
    return std::move(made).value();
}

nucleus::config_space space_with(std::string              name,
                                 std::vector<std::string> allowed)
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("root", nucleus::anchor::root())));
    nucleus::schema_element field = nucleus::element(
            std::move(name), nucleus::anchor::keyspace("root"));
    field.allowed_values = std::move(allowed);
    REQUIRE(builder.register_element(std::move(field)));
    return nucleus::builder_result_test::built(builder);
}

}

TEST_CASE("XML rendering carries valid multibyte names and supplementary text",
          "[xml][emit][grammar]")
{
    const auto rendered = nucleus::xml::render_document_schema_blind(
            config_of("gr"
                      "\xC3\xBC"
                      "n/wert",
                      "\xF0\x90\x80\x80"));
    REQUIRE(rendered);
    CHECK(rendered.value().find("<gr"
                                "\xC3\xBC"
                                "n>") != std::string::npos);
    CHECK(rendered.value().find("\xF0\x90\x80\x80") != std::string::npos);
}

TEST_CASE("XML templates check the wrapper, every declared path and every annotation",
          "[xml][emit][grammar][matrix]")
{
    SECTION("the wrapper is a name")
    {
        const auto rendered = nucleus::xml::render_template(
                space_with("field", {}), "bad name");
        REQUIRE_FALSE(rendered);
        CHECK(rendered.error().code == nucleus::errc::malformed_source);
        CHECK(rendered.error().message.find("space name") != std::string::npos);
    }
    SECTION("a later declared path is reached too")
    {
        const auto rendered = nucleus::xml::render_template(
                space_with("\xC3"
                           "field",
                           {}));
        REQUIRE_FALSE(rendered);
        CHECK(rendered.error().message.find("not a valid XML name") != std::string::npos);
        CHECK(rendered.error().message.find("root") != std::string::npos);
    }
    SECTION("a later allowed value is reached too")
    {
        const auto rendered = nucleus::xml::render_template(
                space_with("field", {"ok", "bad\x01"}));
        REQUIRE_FALSE(rendered);
        CHECK(rendered.error().message.find("allowed value") != std::string::npos);
    }
    SECTION("line breaks stay valid annotation text")
    {
        CHECK(nucleus::xml::render_template(space_with("field", {"a\r\nb"})));
    }
}

TEST_CASE("XML repeated containers name a domain-boundary ordinal gap exactly",
          "[xml][emit][grammar]")
{
    const auto rendered = nucleus::xml::render_document_schema_blind(
            config_of("node[4294967295]/port", "80"));
    REQUIRE_FALSE(rendered);
    CHECK(rendered.error().code == nucleus::errc::malformed_source);
    CHECK(rendered.error().message.find("instance 4294967295") != std::string::npos);
    const auto beyond = nucleus::config::from_values(
            {{"node[4294967296]/port", "80"}});
    REQUIRE_FALSE(beyond);
    CHECK(beyond.error().code == nucleus::errc::malformed_source);
}

TEST_CASE("XML template element names are checked as the template emits them",
          "[xml][emit][grammar]")
{
    SECTION("bracket-index notation never reaches the emitter as a declared name")
    {
        nucleus::config_space_builder builder;
        REQUIRE(builder.register_element(
                nucleus::element("root", nucleus::anchor::root())));
        CHECK_FALSE(builder.register_element(
                nucleus::element("node[0]", nucleus::anchor::keyspace("root"))));
    }
    SECTION("an ordinary declared name still renders")
    {
        const auto rendered = nucleus::xml::render_template(space_with("node", {}));
        REQUIRE(rendered);
        CHECK(rendered.value().find("<node") != std::string::npos);
    }
    SECTION("the document surface still reads an ordinal as a sibling index")
    {
        const auto rendered = nucleus::xml::render_document_schema_blind(
                config_of("node[0]/port", "80"));
        REQUIRE(rendered);
        CHECK(rendered.value().find("<node>") != std::string::npos);
        CHECK(rendered.value().find("node[0]") == std::string::npos);
    }
}

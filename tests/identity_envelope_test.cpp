#include "builder_result_test_support.h"
#include "identity_envelope_test_support.h"

#include "nucleus/error.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace nucleus;
namespace envelope_test = nucleus::identity_envelope_test;

TEST_CASE("xml_source: root mismatch returns malformed_source naming both names",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<other><plugin><x>1</x></plugin></other>", "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    REQUIRE(result.error().message.find("engine") != std::string::npos);
    REQUIRE(result.error().message.find("other") != std::string::npos);
}

TEST_CASE("xml_source: named-space transparency strips root from key paths",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine><plugin><x>1</x></plugin></engine>", "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE(result);
    REQUIRE(result->get("plugin/x") == "1");
    REQUIRE_FALSE(result->get("engine/plugin/x"));
}

TEST_CASE("xml_source: a root-anchored leaf under a named space keeps its value",
          "[identity_envelope][xml]")
{
    config_space_builder builder;
    REQUIRE(builder.register_schema("motd"));
    REQUIRE(builder.register_schema("plugin/x"));
    const config_space space   = nucleus::builder_result_test::built(builder);
    const load_options options = envelope_test::make_options(
            "<engine><motd>hello</motd><plugin><x>1</x></plugin></engine>",
            "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE(result);
    REQUIRE(result->get("motd") == "hello");
    REQUIRE(result->get("plugin/x") == "1");
}

TEST_CASE("xml_source: mixed content on a named-space root is rejected loudly",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine>stray text<plugin><x>1</x></plugin></engine>", "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    REQUIRE(result.error().message.find("mixes character data") != std::string::npos);
}

TEST_CASE("xml_source: pure character data as a named-space root body is rejected loudly",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine>hello</engine>", "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    REQUIRE(result.error().message.find("no representable key") != std::string::npos);
}

TEST_CASE("xml_source: character data beside an attribute on a named-space root is "
          "rejected as mixed content",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine version=\"2\">stray text</engine>", "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    REQUIRE(result.error().message.find("mixes character data") != std::string::npos);
}

TEST_CASE("xml_source: unnamed space keeps root name as first key segment",
          "[identity_envelope][xml]")
{
    config_space_builder builder;
    REQUIRE(builder.register_schema("engine/plugin/x"));
    const config_space space   = nucleus::builder_result_test::built(builder);
    const load_options options = envelope_test::make_options(
            "<engine><plugin><x>1</x></plugin></engine>");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE(result);
    REQUIRE(result->get("engine/plugin/x") == "1");
    REQUIRE_FALSE(result->get("plugin/x"));
}

TEST_CASE("config_space::space_name() returns the name set on the builder",
          "[identity_envelope][space]")
{
    config_space_builder named_builder;
    named_builder.name("mynamespace");
    const config_space named_space = nucleus::builder_result_test::built(named_builder);
    REQUIRE(named_space.space_name() == "mynamespace");

    config_space_builder unnamed_builder;
    const config_space   unnamed_space = nucleus::builder_result_test::built(unnamed_builder);
    REQUIRE(unnamed_space.space_name().empty());
}

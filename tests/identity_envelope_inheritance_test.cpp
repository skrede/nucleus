#include "identity_envelope_test_support.h"

#include "nucleus/error.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace nucleus;
namespace envelope_test = nucleus::identity_envelope_test;

TEST_CASE("xml_source: a non-grammar attribute on a named-space root is discarded, "
          "yielding neither a key nor an error",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine version=\"2\"><plugin><x>1</x></plugin></engine>",
            "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE(result);
    REQUIRE(result->get("plugin/x") == "1");
    REQUIRE_FALSE(result->contains("version"));
    REQUIRE_FALSE(result->contains("engine/version"));
}

TEST_CASE("xml_source: inherit= on a named-space root yields no keyspace entry",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine inherit=\"none\"><plugin><x>1</x></plugin></engine>",
            "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE(result);
    REQUIRE(result->get("plugin/x") == "1");
    REQUIRE_FALSE(result->contains("inherit"));
}

TEST_CASE("xml_source: a duplicate attribute on a named-space root is rejected",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine a=\"1\" a=\"2\"><plugin><x>1</x></plugin></engine>",
            "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    REQUIRE(result.error().message.find("duplicate attribute") != std::string::npos);
}

TEST_CASE("xml_source: inherit= on transparent root is accepted",
          "[identity_envelope][xml]")
{
    const config_space space = envelope_test::plugin_space();
    load_options       options;
    options.document_paths = {"derived.xml"};
    options.make_document  = [](const std::string &path) -> source_handle
    {
        if(path == "derived.xml")
            return envelope_test::xml_of(
                    "<engine inherit=\"base.xml\"><plugin><x>1</x></plugin></engine>",
                    "engine");
        return envelope_test::xml_of(
                "<engine><plugin><x>99</x></plugin></engine>", "engine");
    };

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE(result);
    REQUIRE(result->get("plugin/x") == "1");
}

TEST_CASE("xml_source: inherit= on a non-root child is a malformed_source error",
          "[identity_envelope][xml]")
{
    const config_space space   = envelope_test::plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine><plugin inherit=\"base.xml\"><x>1</x></plugin></engine>",
            "engine");

    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    REQUIRE(result.error().message.find("plugin") != std::string::npos);
}

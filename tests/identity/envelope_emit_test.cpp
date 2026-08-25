#include "identity/envelope_test_support.h"

#include "support/builder_result_test_support.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace nucleus;
namespace envelope_test = nucleus::identity_envelope_test;

TEST_CASE("emit_document + load round-trip preserves a root-anchored leaf under a named space",
          "[identity_envelope][xml][round-trip]")
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("motd", anchor::root())));
    const config_space space   = nucleus::builder_result_test::built(builder);
    const load_options options = envelope_test::make_options(
            "<engine><motd>hello</motd></engine>", "engine");

    const load_result loaded = load_config(space, source_stack{}, options);
    REQUIRE(loaded);
    REQUIRE(loaded->get("motd") == "hello");

    const auto rendered = xml::render_document(loaded.value(), space, "engine");
    REQUIRE(rendered);
    const load_options reload_options = envelope_test::make_options(
            rendered.value(), "engine");
    const load_result reloaded = load_config(
            space, source_stack{}, reload_options);
    REQUIRE(reloaded);
    REQUIRE(reloaded->get("motd") == "hello");
}

TEST_CASE("xml::emit_template with space_name wraps output in a named root element",
          "[identity_envelope][xml][emitter]")
{
    const config_space space    = envelope_test::typed_plugin_space();
    const auto         rendered = xml::render_template(space, "engine");
    REQUIRE(rendered);
    REQUIRE(rendered->find("<engine>") != std::string::npos);
    REQUIRE(rendered->find("</engine>") != std::string::npos);
    REQUIRE(rendered->find("<plugin") != std::string::npos);
}

TEST_CASE("xml::emit_document with space_name wraps the document in a named root element",
          "[identity_envelope][xml][emitter]")
{
    const config_space space   = envelope_test::typed_plugin_space();
    const load_options options = envelope_test::make_options(
            "<engine><plugin><x>42</x></plugin></engine>", "engine");
    const load_result loaded = load_config(space, source_stack{}, options);
    REQUIRE(loaded);

    const auto rendered = xml::render_document(loaded.value(), space, "engine");
    REQUIRE(rendered);
    REQUIRE(rendered->find("<engine>") != std::string::npos);
    REQUIRE(rendered->find("</engine>") != std::string::npos);
    REQUIRE(rendered->find("42") != std::string::npos);
}

TEST_CASE("emit_template + xml_source round-trip with space_name reproduces keys",
          "[identity_envelope][xml][round-trip]")
{
    const config_space space    = envelope_test::typed_plugin_space();
    const auto         rendered = xml::render_template(space, "engine");
    REQUIRE(rendered);
    REQUIRE(rendered->find("<engine>") != std::string::npos);

    const load_options options = envelope_test::make_options(
            rendered.value(), "engine");
    const load_result result = load_config(space, source_stack{}, options);
    REQUIRE(result);
    REQUIRE(result->get("plugin/x") == "");
}

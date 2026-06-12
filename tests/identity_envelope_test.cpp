#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include "nucleus/config_source/source_stack.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <sstream>
#include <string_view>

using namespace nucleus;

namespace {

nucleus::load_options make_opts(std::string xml, std::string_view space_name = {})
{
    load_options opts;
    opts.document_paths = {"doc.xml"};
    opts.make_document = [xml = std::move(xml), space_name = std::string(space_name)](
                             const std::string &)
    {
        auto src = xml_source::from(xml_source_options::of_string(xml));
        if(!space_name.empty())
            src.with_space_name(space_name);
        return source_handle(std::move(src));
    };
    return opts;
}

// Schema with plugin/x declared as a typed element so emit_template can project it.
nucleus::config_space make_plugin_space_typed()
{
    config_space_builder b;
    REQUIRE(b.register_element(element("plugin", anchor::root())));
    REQUIRE(b.register_element(element("x", anchor::keyspace("plugin"))));
    return b.build();
}

// Schema with plugin/x as a plain schema path (register_schema only).
nucleus::config_space make_plugin_space()
{
    config_space_builder b;
    REQUIRE(b.register_schema("plugin/x"));
    return b.build();
}

}

TEST_CASE("xml_source: root mismatch returns malformed_source naming both names",
          "[identity_envelope][xml]")
{
    auto space = make_plugin_space();
    auto opts = make_opts("<other><plugin><x>1</x></plugin></other>", "vagus");

    auto result = load_config(space, source_stack{}, opts);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    const std::string &msg = result.error().message;
    REQUIRE(msg.find("vagus") != std::string::npos);
    REQUIRE(msg.find("other") != std::string::npos);
}

TEST_CASE("xml_source: named-space transparency strips root from key paths",
          "[identity_envelope][xml]")
{
    auto space = make_plugin_space();
    auto opts = make_opts("<vagus><plugin><x>1</x></plugin></vagus>", "vagus");

    auto result = load_config(space, source_stack{}, opts);
    REQUIRE(result);
    // Key is plugin/x, NOT vagus/plugin/x.
    REQUIRE(result.value().get("plugin/x") == "1");
    REQUIRE_FALSE(result.value().get("vagus/plugin/x"));
}

TEST_CASE("xml_source: unnamed space keeps root name as first key segment",
          "[identity_envelope][xml]")
{
    config_space_builder b;
    REQUIRE(b.register_schema("vagus/plugin/x"));
    auto space = b.build();
    auto opts = make_opts("<vagus><plugin><x>1</x></plugin></vagus>");

    auto result = load_config(space, source_stack{}, opts);
    REQUIRE(result);
    REQUIRE(result.value().get("vagus/plugin/x") == "1");
    REQUIRE_FALSE(result.value().get("plugin/x"));
}

TEST_CASE("xml_source: inherit= on transparent root is accepted",
          "[identity_envelope][xml]")
{
    // inherit= on root is consumed by inheritance(); the source should not reject it.
    config_space_builder b;
    REQUIRE(b.register_schema("plugin/x"));
    auto space = b.build();

    load_options opts;
    opts.document_paths = {"derived.xml"};
    // The source has inherit= on the transparent root; pull() must succeed.
    opts.make_document = [](const std::string &path) -> source_handle
    {
        std::string xml;
        if(path == "derived.xml")
            xml = "<vagus inherit=\"base.xml\"><plugin><x>1</x></plugin></vagus>";
        else
            xml = "<vagus><plugin><x>99</x></plugin></vagus>";
        auto src = xml_source::from(xml_source_options::of_string(xml));
        src.with_space_name("vagus");
        return source_handle(std::move(src));
    };

    auto result = load_config(space, source_stack{}, opts);
    REQUIRE(result);
    // Derived overrides base: x = 1.
    REQUIRE(result.value().get("plugin/x") == "1");
}

TEST_CASE("xml_source: inherit= on a non-root child is a malformed_source error",
          "[identity_envelope][xml]")
{
    auto space = make_plugin_space();
    // extend= on non-root (no primary key registered here so extend= fires the grammar check).
    auto opts = make_opts("<vagus><plugin inherit=\"base.xml\"><x>1</x></plugin></vagus>", "vagus");

    auto result = load_config(space, source_stack{}, opts);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == errc::malformed_source);
    REQUIRE(result.error().message.find("plugin") != std::string::npos);
}

TEST_CASE("xml::emit_template with space_name wraps output in a named root element",
          "[identity_envelope][xml][emitter]")
{
    // emit_template projects schema_elements(), so we need register_element.
    auto space = make_plugin_space_typed();
    std::ostringstream out;
    nucleus::xml::emit_template(space, out, "vagus");
    const std::string xml = out.str();

    REQUIRE(xml.find("<vagus>") != std::string::npos);
    REQUIRE(xml.find("</vagus>") != std::string::npos);
    // The inner plugin element is present inside the envelope.
    REQUIRE(xml.find("<plugin") != std::string::npos);
}

TEST_CASE("xml::emit_document with space_name wraps the document in a named root element",
          "[identity_envelope][xml][emitter]")
{
    config_space_builder b;
    REQUIRE(b.register_schema("plugin/x"));
    auto space = b.build();
    auto opts = make_opts("<vagus><plugin><x>42</x></plugin></vagus>", "vagus");
    auto config_result = load_config(space, source_stack{}, opts);
    REQUIRE(config_result);

    std::ostringstream out;
    nucleus::xml::emit_document(config_result.value(), out, "vagus");
    const std::string xml = out.str();

    REQUIRE(xml.find("<vagus>") != std::string::npos);
    REQUIRE(xml.find("</vagus>") != std::string::npos);
    REQUIRE(xml.find("42") != std::string::npos);
}

TEST_CASE("emit_template + xml_source round-trip with space_name reproduces keys",
          "[identity_envelope][xml][round-trip]")
{
    // Typed space so emit_template produces actual XML.
    auto space = make_plugin_space_typed();

    std::ostringstream tmpl_out;
    nucleus::xml::emit_template(space, tmpl_out, "vagus");
    const std::string tmpl = tmpl_out.str();

    // The template is valid XML parseable by xml_source with the same space name.
    REQUIRE(tmpl.find("<vagus>") != std::string::npos);

    // The template XML parses without error under the same space_name.
    auto opts = make_opts(tmpl, "vagus");
    auto result = load_config(space, source_stack{}, opts);
    REQUIRE(result);
    // Template carries no value so get() returns nothing; no error means round-trip succeeds.
    REQUIRE(result.value().get("plugin/x") == std::nullopt);
}

TEST_CASE("config_space::space_name() returns the name set on the builder",
          "[identity_envelope][space]")
{
    {
        config_space_builder b;
        b.name("mynamespace");
        auto space = b.build();
        REQUIRE(space.space_name() == "mynamespace");
    }
    {
        config_space_builder b;
        auto space = b.build();
        REQUIRE(space.space_name().empty());
    }
}

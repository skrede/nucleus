#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"
#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>

using namespace nucleus;

namespace {

source_handle xml_of(std::string text)
{
    return source_handle(
            xml_source::from(xml_source_options::of_string(std::move(text))));
}

config_space make_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("server", anchor::root())));
    REQUIRE(builder.register_element(
            element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(enum_element(
            "mode", anchor::keyspace("server"),
            std::vector<std::string>{"primary", "secondary"})));
    REQUIRE(builder.register_element(
            repeated_element("tag", anchor::keyspace("server"))));
    return nucleus::builder_result_test::built(builder);
}

}

TEST_CASE("round-trip: runtime_source + XML repeated field -> emit -> reload is lossless",
          "[system][round_trip]")
{
    const config_space space = make_space();

    runtime_source base;
    base.set("server/host", "localhost").set("server/mode", "primary");

    load_options options;
    options.document_paths = {"config.xml"};
    options.make_document  = [](const std::string &) -> source_handle
    {
        return xml_of("<server><tag>alpha</tag><tag>beta</tag></server>");
    };

    const load_result first = load_config(
            space, source_stack{std::move(base)}, options);
    REQUIRE(first);
    const config &initial = first.value();

    const auto rendered = xml::render_document(initial, space);
    REQUIRE(rendered);
    REQUIRE_FALSE(rendered->empty());

    load_options reload_options;
    reload_options.document_paths = {"emitted.xml"};
    reload_options.make_document  = [&rendered](const std::string &) -> source_handle
    {
        return xml_of(rendered.value());
    };

    const load_result second = load_config(
            space, source_stack{}, reload_options);
    REQUIRE(second);
    const config &reloaded = second.value();

    REQUIRE(reloaded.keys() == initial.keys());
    REQUIRE(reloaded.get("server/host") == initial.get("server/host"));
    REQUIRE(reloaded.get("server/mode") == initial.get("server/mode"));

    const std::vector<std::string> initial_tags  = initial.get_all("server/tag");
    const std::vector<std::string> reloaded_tags = reloaded.get_all("server/tag");
    REQUIRE(reloaded_tags.size() == initial_tags.size());
    REQUIRE(reloaded_tags == initial_tags);
    REQUIRE(initial_tags == std::vector<std::string>{"alpha", "beta"});
    REQUIRE(reloaded_tags == std::vector<std::string>{"alpha", "beta"});
}

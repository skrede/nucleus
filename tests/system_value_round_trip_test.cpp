#include "nucleus/config.h"
#include "builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/env/env_source.h"
#include "nucleus/env/env_emitter.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <sstream>
#include <utility>

using namespace nucleus;

namespace {

source_handle xml_of(std::string text)
{
    return source_handle(
            xml_source::from(xml_source_options::of_string(std::move(text))));
}

config_space scalar_space()
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

TEST_CASE("round-trip: all scalar values survive emit -> reload unchanged",
          "[system][round_trip]")
{
    const config_space space = scalar_space();
    runtime_source     source;
    source.set("server/host", "edge-node").set("server/mode", "secondary");

    const load_result first = load_config(
            space, source_stack{std::move(source)}, {});
    REQUIRE(first);
    const config &initial = first.value();

    const auto rendered = xml::render_document(initial, space);
    REQUIRE(rendered);
    REQUIRE_FALSE(rendered->empty());

    load_options options;
    options.document_paths = {"out.xml"};
    options.make_document  = [&rendered](const std::string &) -> source_handle
    {
        return xml_of(rendered.value());
    };

    const load_result second = load_config(space, source_stack{}, options);
    REQUIRE(second);
    REQUIRE(second->keys() == initial.keys());
    for(const std::string &key : initial.keys())
        REQUIRE(second->get_all(key) == initial.get_all(key));
}

TEST_CASE("fidelity: empty, whitespace, comment-split and CDATA leaves read as expected",
          "[system][round_trip][fidelity]")
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("server", anchor::root())));
    REQUIRE(builder.register_element(element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(element("motd", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(element("blank", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(element("port", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(element("note", anchor::keyspace("server"))));
    const config_space space = nucleus::builder_result_test::built(builder);

    const std::string document =
            "<server><host>localhost</host><motd></motd><blank>   </blank>"
            "<port>8<!-- keep the default -->080</port>"
            "<note>a<![CDATA[b]]>c</note></server>";
    load_options options;
    options.document_paths = {"config.xml"};
    options.make_document  = [&document](const std::string &) -> source_handle
    {
        return xml_of(document);
    };

    const load_result loaded = load_config(space, source_stack{}, options);
    REQUIRE(loaded);
    REQUIRE(loaded->get("server/host") == "localhost");
    REQUIRE(loaded->get("server/port") == "8080");
    REQUIRE(loaded->get("server/note") == "abc");
    REQUIRE(loaded->get("server/motd") == "");
    REQUIRE(loaded->get("server/blank") == "");
}

TEST_CASE("round-trip: an empty-string value survives emit -> reload",
          "[system][round_trip][fidelity]")
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("server", anchor::root())));
    REQUIRE(builder.register_element(element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(element("motd", anchor::keyspace("server"))));
    const config_space space = nucleus::builder_result_test::built(builder);

    runtime_source source;
    source.set("server/host", "localhost").set("server/motd", "");
    const load_result first = load_config(
            space, source_stack{std::move(source)}, {});
    REQUIRE(first);
    REQUIRE(first->get("server/motd") == "");

    const auto rendered = xml::render_document(first.value(), space);
    REQUIRE(rendered);
    load_options options;
    options.document_paths = {"out.xml"};
    options.make_document  = [&rendered](const std::string &) -> source_handle
    {
        return xml_of(rendered.value());
    };

    const load_result second = load_config(space, source_stack{}, options);
    REQUIRE(second);
    REQUIRE(second->keys() == first->keys());
    REQUIRE(second->get("server/motd") == "");
    REQUIRE(second->get("server/host") == "localhost");
}

TEST_CASE("round-trip via env emitter: scalar subset reloads its keys",
          "[system][round_trip]")
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("host", anchor::root())));
    REQUIRE(builder.register_element(element("port", anchor::root())));
    const config_space space = nucleus::builder_result_test::built(builder);

    env_source source;
    source.set("host", "rt-host").set("port", "5050");
    const load_result first = load_config(
            space, source_stack{std::move(source)}, {});
    REQUIRE(first);

    const auto rendered = env::render_document(first.value());
    REQUIRE(rendered);
    REQUIRE_FALSE(rendered->empty());

    env_source         replay;
    std::istringstream lines(rendered.value());
    for(std::string line; std::getline(lines, line);)
    {
        if(line.empty() || line.front() == '#')
            continue;
        const std::size_t separator = line.find('=');
        if(separator != std::string::npos)
            replay.set(line.substr(0, separator), line.substr(separator + 1));
    }

    const load_result second = load_config(
            space, source_stack{std::move(replay)}, {});
    REQUIRE(second);
    REQUIRE(second->get("host") == first->get("host"));
    REQUIRE(second->get("port") == first->get("port"));
    REQUIRE(second->keys() == first->keys());
}

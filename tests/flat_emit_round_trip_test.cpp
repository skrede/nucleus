#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/argv_emitter.h"

#include "nucleus/env/env_source.h"
#include "nucleus/env/env_emitter.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <utility>

namespace {

constexpr std::array<std::size_t, 4> ordinals{0, 1, 2, 10};

nucleus::cli_delimiter delimiter()
{
    auto parsed = nucleus::cli_delimiter::parse("__");
    REQUIRE(parsed);
    return std::move(parsed).value();
}

nucleus::key_path cluster_anchor()
{
    auto parsed = nucleus::key_path::parse("cluster");
    REQUIRE(parsed);
    return std::move(parsed).value();
}

std::vector<std::string> lines_of(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream       input(text);
    for(std::string line; std::getline(input, line);)
        lines.push_back(std::move(line));
    return lines;
}

nucleus::config_space nested_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
            nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("route", nucleus::anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(nucleus::typed_element<std::int32_t>(
            "port", nucleus::anchor::keyspace("cluster/node/route"))));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space typed_root_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::typed_element<std::int32_t>(
            "port", nucleus::anchor::root())));
    return nucleus::builder_result_test::built(builder);
}

std::string port_path(std::size_t outer, std::size_t inner)
{
    return "cluster/node[" + std::to_string(outer) + "]/route[" + std::to_string(inner) + "]/port";
}

nucleus::runtime_source nested_source(std::int32_t offset)
{
    nucleus::runtime_source source;
    for(std::size_t outer : ordinals)
        for(std::size_t inner : ordinals)
            source.set(port_path(outer, inner),
                       std::to_string(offset + static_cast<std::int32_t>((outer * 100) + inner)));
    return source;
}

std::vector<std::string> expected_strings()
{
    std::vector<std::string> values;
    for(std::size_t outer : ordinals)
        for(std::size_t inner : ordinals)
            values.push_back(std::to_string((outer * 100) + inner));
    return values;
}

nucleus::load_result replay_argv(const nucleus::config_space &space,
                                 const std::string           &artifact)
{
    nucleus::argv_source replay(lines_of(artifact));
    replay.delimit_with(delimiter())
            .anchor_at(cluster_anchor())
            .recognize_with(nucleus::recognizer_of(space));
    return nucleus::load_config(
            space, nucleus::source_stack{nested_source(20000), std::move(replay)}, {});
}

nucleus::load_result replay_environment(const nucleus::config_space &space,
                                        const std::string           &artifact)
{
    nucleus::env_source replay;
    for(const std::string &line : lines_of(artifact))
    {
        const std::size_t split = line.find('=');
        REQUIRE(split != std::string::npos);
        replay.set(line.substr(0, split), line.substr(split + 1));
    }
    return nucleus::load_config(
            space, nucleus::source_stack{nested_source(20000), std::move(replay)}, {});
}

void require_semantic_replay(const nucleus::config &original,
                             const nucleus::config &replayed)
{
    REQUIRE(replayed.size() == ordinals.size() * ordinals.size());
    REQUIRE(replayed.keys() == original.keys());
    for(const std::string &key : original.keys())
    {
        CHECK(replayed.get(key) == original.get(key));
        REQUIRE(original.provenance_of(key) != nullptr);
        REQUIRE(replayed.provenance_of(key) != nullptr);
        CHECK(original.provenance_of(key)->layer == "stack[0]");
        CHECK(replayed.provenance_of(key)->layer == "stack[1]");
    }
    CHECK(replayed.get_all("cluster/node/route/port") == expected_strings());
    const auto typed          = replayed.get_all_as<std::int32_t>("cluster/node/route/port");
    const auto original_typed = original.get_all_as<std::int32_t>("cluster/node/route/port");
    REQUIRE(typed);
    REQUIRE(original_typed);
    CHECK(typed.value() == original_typed.value());
    CHECK(replayed.degradations().empty());
}

}

TEST_CASE("nested argv and environment artifacts replay exact semantic snapshots",
          "[flat][emit][round_trip][ordinal]")
{
    const nucleus::config_space space  = nested_space();
    const nucleus::load_result  loaded = nucleus::load_config(
            space, nucleus::source_stack{nested_source(0)}, {});
    REQUIRE(loaded);

    const auto argv = nucleus::argv::render_document(
            loaded.value(), delimiter(), cluster_anchor());
    const auto environment = nucleus::env::render_document(loaded.value());
    REQUIRE(argv);
    REQUIRE(environment);
    REQUIRE(lines_of(argv.value()).size() == loaded->size());
    REQUIRE(lines_of(environment.value()).size() == loaded->size());

    const nucleus::load_result argv_replayed = replay_argv(space, argv.value());
    const nucleus::load_result env_replayed  = replay_environment(space, environment.value());
    REQUIRE(argv_replayed);
    REQUIRE(env_replayed);
    require_semantic_replay(loaded.value(), argv_replayed.value());
    require_semantic_replay(loaded.value(), env_replayed.value());
}

TEST_CASE("flat replay recomputes degradation metadata",
          "[flat][emit][round_trip][metadata]")
{
    const nucleus::config_space space = typed_root_space();
    nucleus::env_source         original_source;
    original_source.set("port", "7000");
    const nucleus::load_result original = nucleus::load_config(
            space, nucleus::source_stack{std::move(original_source)}, {});
    REQUIRE(original);
    REQUIRE(original->degradations().size() == 1);

    const auto rendered = nucleus::env::render_document(original.value());
    REQUIRE(rendered);
    REQUIRE(rendered.value() == "port=7000\n");
    nucleus::runtime_source base;
    base.set("port", "1");
    nucleus::env_source replay;
    replay.set("port", lines_of(rendered.value()).front().substr(5));
    const nucleus::load_result reloaded = nucleus::load_config(
            space, nucleus::source_stack{std::move(base), std::move(replay)}, {});
    REQUIRE(reloaded);
    REQUIRE(reloaded->degradations().empty());
    REQUIRE(reloaded->get_as<std::int32_t>("port") == original->get_as<std::int32_t>("port"));
    REQUIRE(reloaded->provenance_of("port") != nullptr);
    REQUIRE(reloaded->provenance_of("port")->layer == "stack[1]");
}

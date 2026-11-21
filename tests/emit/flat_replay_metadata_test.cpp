#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "support/builder_result_test_support.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/converters.h"

#include "nucleus/env/env_source.h"
#include "nucleus/env/env_emitter.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <utility>

namespace {

std::vector<std::string> lines_of(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream       input(text);
    for(std::string line; std::getline(input, line);)
        lines.push_back(std::move(line));
    return lines;
}

nucleus::config_space typed_root_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::typed_element<std::int32_t>(
            "port", nucleus::anchor::root())));
    return nucleus::builder_result_test::built(builder);
}

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

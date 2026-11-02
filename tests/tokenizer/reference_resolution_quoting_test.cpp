#include "support/builder_result_test_support.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::runtime_source;
using nucleus::source_stack;

TEST_CASE("a closing brace inside a quoted fallback arm does not end the tree token",
          "[reference][quoting]")
{
    auto           space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/alias", "${abs:cluster/absent ?? \"}\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias").value() == "}");
}

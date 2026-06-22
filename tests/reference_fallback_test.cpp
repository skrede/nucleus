#include "nucleus/config_space.h"
#include "nucleus/identity.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// REF-07 acceptance tests: ?? fallback chaining.
// ?? catches missing reference only; parse/cycle/budget errors propagate.
// Arms are evaluated left-to-right; first success wins.

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::runtime_source;
using nucleus::source_stack;

TEST_CASE("?? falls through to second arm when first is absent (REF-07)", "[reference][fallback]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/port", "8080");
    // First arm (abs:host/missing) is absent; second arm resolves.
    src.set("host/result", "${abs:host/missing ?? abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "8080");
}

TEST_CASE("?? uses literal string floor when all ref arms are absent (REF-07)",
          "[reference][fallback]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/result", "${abs:host/missing ?? \"9090\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "9090");
}

TEST_CASE("?? left-to-right: first present value wins (REF-07)", "[reference][fallback]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/primary", "primary-value");
    src.set("host/secondary", "secondary-value");
    // primary is present -- secondary must not be evaluated.
    src.set("host/result", "${abs:host/primary ?? abs:host/secondary}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "primary-value");
}

TEST_CASE("cycle error propagates past all ?? arms (REF-07 / D-05)", "[reference][fallback]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    // cyclic reference -- the ?? fallback must NOT swallow this.
    src.set("loop/a", "${abs:loop/b ?? \"safe\"}");
    src.set("loop/b", "${abs:loop/a}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("cyclic") != std::string::npos);
}

TEST_CASE("no ?? and missing reference is a hard error (REF-07)", "[reference][fallback]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/result", "${abs:host/missing}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("absent") != std::string::npos);
}

TEST_CASE("three-arm ?? chain falls through to the literal floor (REF-07)",
          "[reference][fallback]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/result", "${abs:host/a ?? abs:host/b ?? \"default\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "default");
}

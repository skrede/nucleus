#include "support/builder_result_test_support.h"

#include "nucleus/error.h"
#include "nucleus/identity.h"
#include "nucleus/config_space.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstdint>
#include <utility>

// Acceptance tests: ?? fallback chaining.
// ?? catches missing reference only; parse/cycle/budget errors propagate.
// Arms are evaluated left-to-right; first success wins.

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::runtime_source;
using nucleus::source_stack;

namespace {

nucleus::load_result load_with_emit(const std::string &value, std::int32_t &calls)
{
    auto emit = [&calls](const nucleus::tree_access &) -> nucleus::token_result
    {
        ++calls;
        return std::string("EMIT");
    };

    config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(nucleus::tree_tokenizer("emit", emit)));
    runtime_source src;
    src.set("a/v", value);
    return load_config(nucleus::builder_result_test::built(engine),
                       source_stack{std::move(src)}, {});
}

std::string result_text(const nucleus::load_result &loaded)
{
    return loaded ? loaded.value().get("a/v").value_or("") : loaded.error().message;
}

void require_refused(const std::string &value, const std::string &token)
{
    std::int32_t calls = 0;
    auto const loaded = load_with_emit(value, calls);
    INFO("input: " << value);
    INFO("result: " << result_text(loaded));
    INFO("resolver calls: " << calls);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == nucleus::errc::unresolved_token);
    CHECK(loaded.error().message.find("takes no argument list") != std::string::npos);
    CHECK(loaded.error().message.find(token) != std::string::npos);
    CHECK(calls == 0);
}

void require_dispatched(const std::string &value)
{
    std::int32_t calls = 0;
    auto const loaded = load_with_emit(value, calls);
    INFO("input: " << value);
    INFO("result: " << result_text(loaded));
    INFO("resolver calls: " << calls);
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("a/v") == "EMIT");
    CHECK(calls == 1);
}

}

TEST_CASE("?? falls through to second arm when first is absent", "[reference][fallback]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/port", "8080");
    // First arm (abs:host/missing) is absent; second arm resolves.
    src.set("host/result", "${abs:host/missing ?? abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "8080");
}

TEST_CASE("?? uses literal string floor when all ref arms are absent",
          "[reference][fallback]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/result", "${abs:host/missing ?? \"9090\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "9090");
}

TEST_CASE("?? left-to-right: first present value wins", "[reference][fallback]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/primary", "primary-value");
    src.set("host/secondary", "secondary-value");
    // primary is present -- secondary must not be evaluated.
    src.set("host/result", "${abs:host/primary ?? abs:host/secondary}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "primary-value");
}

TEST_CASE("cycle error propagates past all ?? arms", "[reference][fallback]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    // cyclic reference -- the ?? fallback must NOT swallow this.
    src.set("loop/a", "${abs:loop/b ?? \"safe\"}");
    src.set("loop/b", "${abs:loop/a}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("cyclic") != std::string::npos);
}

TEST_CASE("no ?? and missing reference is a hard error", "[reference][fallback]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/result", "${abs:host/missing}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("absent") != std::string::npos);
}

TEST_CASE("three-arm ?? chain falls through to the literal floor",
          "[reference][fallback]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/result", "${abs:host/a ?? abs:host/b ?? \"default\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/result") == "default");
}

TEST_CASE("a tree-tokenizer token carrying an argument list is refused before dispatch",
          "[reference][fallback]")
{
    require_refused("${emit.f(x=1)}", "${emit.f(x=1)}");
}

TEST_CASE("an argument list in a fallback arm is refused, not dropped", "[reference][fallback]")
{
    require_refused("${abs:nope ?? emit.f(x=1)}", "${emit.f(x=1)}");
}

TEST_CASE("an argument-list refusal ends the fallback chain", "[reference][fallback]")
{
    require_refused("${emit.f()}", "${emit.f()}");
    require_refused("${emit.f() ?? \"y\"}", "${emit.f()}");
    require_refused("${emit.f(x=1) ?? \"y\"}", "${emit.f(x=1)}");
}

TEST_CASE("an argument-list refusal precedes the category lookup", "[reference][fallback]")
{
    std::int32_t calls = 0;
    auto const loaded = load_with_emit("${abs:nope ?? typo.f(x=1)}", calls);
    INFO("result: " << result_text(loaded));
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("takes no argument list") != std::string::npos);
    CHECK(loaded.error().message.find("unknown tree tokenizer category") == std::string::npos);
}

TEST_CASE("a tree-tokenizer token without an argument list still dispatches",
          "[reference][fallback]")
{
    require_dispatched("${emit.f}");
    require_dispatched("${abs:nope ?? emit.f}");
}

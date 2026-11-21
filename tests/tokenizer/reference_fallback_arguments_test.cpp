#include "support/builder_result_test_support.h"

#include "nucleus/error.h"
#include "nucleus/config_space.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include "nucleus/config_source/source_stack.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstdint>
#include <utility>

using nucleus::config_space_builder;
using nucleus::load_config;
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

void require_result(const std::string &value, const std::string &expected)
{
    std::int32_t calls = 0;
    auto const loaded = load_with_emit(value, calls);
    INFO("input: " << value);
    INFO("resolver calls: " << calls);
    CHECK(result_text(loaded) == expected);
    CHECK(calls == 0);
}

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

TEST_CASE("a ?? inside an argument list does not split the arm", "[reference][fallback]")
{
    require_refused("${abs:nope ?? emit.f(x=a??b)}", "${emit.f(x=a??b)}");
}

TEST_CASE("a ?? inside an argument list does not split a whole token", "[reference][fallback]")
{
    require_refused("${emit.f(x=a??b)}", "${emit.f(x=a??b)}");
}

TEST_CASE("an argument list that does not lex is refused", "[reference][fallback]")
{
    require_refused("${abs:nope ?? see.note(1)}", "${see.note(1)}");
}

TEST_CASE("an argument list that never closes is refused", "[reference][fallback]")
{
    require_refused("${abs:nope ?? x.y(a ?? \"z\"}", "${x.y(a ?? \"z\"}");
}

TEST_CASE("floor text holding a dot and a parenthesis keeps its reading", "[reference][fallback]")
{
    require_result("${abs:nope ?? \"see.note(1)\"}", "see.note(1)");
    require_result("${abs:nope ?? \"a(b.c)\"}", "a(b.c)");
    require_result("${abs:nope ?? (a??b)}", "(a??b)");
    require_result("${abs:nope ?? a(b.c)}",
                   "reference resolution failed for 'a/v': unknown tree tokenizer category 'a(b'");
}

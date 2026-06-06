#include "nucleus/identity.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using nucleus::owner_token;
using nucleus::resolve_errc;
using nucleus::resolve_tokens;
using nucleus::tokenizer_builder;
using nucleus::tokenizer_registry;

namespace {

// A category whose every field re-emits the same token, forcing a self cycle:
// ${loop.x} -> "${loop.x}" -> ... The cycle guard must halt this with a named
// error rather than recursing forever.
tokenizer_registry self_referential_registry()
{
    tokenizer_registry r;
    tokenizer_builder b("loop");
    b.set_wildcard([](std::string_view name) -> nucleus::token_result {
        return std::string("${loop.") + std::string(name) + "}";
    });
    r.add(std::move(b).build(), owner_token{});
    return r;
}

// Two categories that bounce between each other: ${ping.x} -> ${pong.x} ->
// ${ping.x}, an a -> b -> a cycle the chain message must name.
tokenizer_registry mutual_registry()
{
    tokenizer_registry r;
    tokenizer_builder ping("ping");
    ping.set_wildcard([](std::string_view name) -> nucleus::token_result {
        return std::string("${pong.") + std::string(name) + "}";
    });
    tokenizer_builder pong("pong");
    pong.set_wildcard([](std::string_view name) -> nucleus::token_result {
        return std::string("${ping.") + std::string(name) + "}";
    });
    r.add(std::move(ping).build(), owner_token{});
    r.add(std::move(pong).build(), owner_token{});
    return r;
}

}

TEST_CASE("a self-referential token fails loudly with a named cycle error", "[resolve][cycle]")
{
    auto reg = self_referential_registry();
    auto r = resolve_tokens("${loop.x}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::cyclic_reference);
    CHECK(r.error().message.find("cyclic reference") != std::string::npos);
    CHECK(r.error().message.find("${loop.x}") != std::string::npos);
}

TEST_CASE("a mutual cycle names the ordered chain", "[resolve][cycle]")
{
    auto reg = mutual_registry();
    auto r = resolve_tokens("${ping.a}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::cyclic_reference);
    CHECK(r.error().message.find("->") != std::string::npos);
}

TEST_CASE("deep but acyclic nesting past the cap is depth_exceeded not a crash", "[resolve][depth]")
{
    // Each level emits a DISTINCT token so the cycle guard never fires; only the
    // depth cap can stop it. Builds ${depth.0} -> ${depth.1} -> ... unbounded.
    tokenizer_registry reg;
    tokenizer_builder b("depth");
    b.set_wildcard([](std::string_view name) -> nucleus::token_result {
        long n = std::stol(std::string(name));
        return std::string("${depth.") + std::to_string(n + 1) + "}";
    });
    reg.add(std::move(b).build(), owner_token{});

    auto r = resolve_tokens("${depth.0}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::depth_exceeded);
}

TEST_CASE("sibling tokens reusing a label after return stay clear of the cycle guard",
          "[resolve][cycle]")
{
    // ${echo.same} ${echo.same} -- the same label appears twice but sequentially,
    // not re-entrantly, so the guard (which pops on return) must not flag it.
    tokenizer_registry reg;
    tokenizer_builder b("echo");
    b.set_wildcard([](std::string_view name) -> nucleus::token_result {
        return std::string(name);
    });
    reg.add(std::move(b).build(), owner_token{});

    auto r = resolve_tokens("${echo.same}-${echo.same}", reg);
    REQUIRE(r.has_value());
    CHECK(r.value() == "same-same");
}

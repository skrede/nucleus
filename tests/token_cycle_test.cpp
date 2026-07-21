#include "nucleus/identity.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstddef>
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

// A wildcard whose emission carries a literal prefix before the self-reference
// (x${loop.x}): the splice-point re-expansion resumes past the "x" but must still
// re-enter under the live guard, so the self-reference stays a named cycle.
tokenizer_registry prefixed_self_referential_registry()
{
    tokenizer_registry r;
    tokenizer_builder b("loop");
    b.set_wildcard([](std::string_view name) -> nucleus::token_result {
        return std::string("x${loop.") + std::string(name) + "}";
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

// A wildcard that fans out: name n emits four copies of ${f.(n+1)} until a leaf
// depth, so the expansion tree branches four ways per level with distinct labels
// (no cycle) and bounded depth. Without a substitution-count budget this exhausts
// memory; the budget must trip first.
tokenizer_registry fanout_registry()
{
    tokenizer_registry r;
    tokenizer_builder b("f");
    b.set_wildcard([](std::string_view name) -> nucleus::token_result {
        long n = std::stol(std::string(name));
        if(n >= 10)
            return std::string("x");
        std::string const child = std::string("${f.") + std::to_string(n + 1) + "}";
        return child + child + child + child;
    });
    r.add(std::move(b).build(), owner_token{});
    return r;
}

// A linear chain of distinct labels performing exactly stop+1 substitutions:
// ${count.0} -> ${count.1} -> ... -> ${count.stop} -> "end". Lets the budget
// boundary be proven deterministically without running the full default cap.
tokenizer_registry counting_registry(long stop)
{
    tokenizer_registry r;
    tokenizer_builder b("count");
    b.set_wildcard([stop](std::string_view name) -> nucleus::token_result {
        long n = std::stol(std::string(name));
        if(n >= stop)
            return std::string("end");
        return std::string("${count.") + std::to_string(n + 1) + "}";
    });
    r.add(std::move(b).build(), owner_token{});
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

TEST_CASE("a self-reference behind a literal prefix still fails as a named cycle", "[resolve][cycle]")
{
    // The splice-point resume must keep re-expanding the produced tail under the
    // producing token's live guard; if it did not, this would recurse forever.
    auto reg = prefixed_self_referential_registry();
    auto r = resolve_tokens("${loop.x}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::cyclic_reference);
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

TEST_CASE("a bounded-depth fanout bomb fails with budget_exceeded rather than exhausting memory",
          "[resolve][budget]")
{
    // Four-way fanout under the depth cap: previously ran to memory exhaustion,
    // now the per-load substitution budget (default) trips long before.
    auto reg = fanout_registry();
    auto r = resolve_tokens("${f.0}", reg);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == resolve_errc::budget_exceeded);
}

TEST_CASE("the substitution budget boundary holds in both directions", "[resolve][budget]")
{
    // counting_registry(stop) performs stop+1 substitutions, so stop = cap-1 hits
    // the cap exactly and stop = cap performs one too many.
    constexpr std::size_t cap = 5;

    SECTION("exactly cap substitutions succeed")
    {
        auto reg = counting_registry(static_cast<long>(cap) - 1);
        nucleus::substitution_budget budget(cap);
        auto r = resolve_tokens("${count.0}", reg, budget);
        REQUIRE(r.has_value());
        CHECK(r.value() == "end");
    }

    SECTION("cap+1 substitutions fail with budget_exceeded")
    {
        auto reg = counting_registry(static_cast<long>(cap));
        nucleus::substitution_budget budget(cap);
        auto r = resolve_tokens("${count.0}", reg, budget);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == resolve_errc::budget_exceeded);
    }
}

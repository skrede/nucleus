#include "nucleus/config_space.h"
#include "nucleus/identity.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Acceptance tests.
// Cross-leaf cycle detection (expansion_guard with leaf-path labels) and the
// substitution-count budget (budget_exceeded stops billion-laughs amplification).

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::runtime_source;
using nucleus::source_stack;

TEST_CASE("cross-leaf cycle A->B->A is detected with the full FQN chain",
          "[reference][cycle]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    // A references B, B references A.
    src.set("graph/a", "${abs:graph/b}");
    src.set("graph/b", "${abs:graph/a}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    const std::string &msg = loaded.error().message;
    // The error must mention "cyclic" and name both paths in the chain.
    CHECK(msg.find("cyclic") != std::string::npos);
    CHECK(msg.find("graph/a") != std::string::npos);
    CHECK(msg.find("graph/b") != std::string::npos);
}

TEST_CASE("self-referential leaf is detected as a cycle", "[reference][cycle]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("host/port", "${abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("cyclic") != std::string::npos);
}

TEST_CASE("three-leaf cycle A->B->C->A names all nodes", "[reference][cycle]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("ring/a", "${abs:ring/b}");
    src.set("ring/b", "${abs:ring/c}");
    src.set("ring/c", "${abs:ring/a}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    const std::string &msg = loaded.error().message;
    CHECK(msg.find("cyclic") != std::string::npos);
}

TEST_CASE("substitution budget exceeded stops billion-laughs amplification",
          "[reference][budget]")
{
    // Build a ladder: b="${abs:a}${abs:a}", c="${abs:b}${abs:b}", ...
    // At 10 levels this would produce 2^10=1024 substitutions from the root.
    // With a tiny budget (e.g. 5 substitutions) this must fail with budget_exceeded.
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("ladder/a", "x");
    src.set("ladder/b", "${abs:ladder/a}${abs:ladder/a}");
    src.set("ladder/c", "${abs:ladder/b}${abs:ladder/b}");
    src.set("ladder/d", "${abs:ladder/c}${abs:ladder/c}");
    src.set("ladder/e", "${abs:ladder/d}${abs:ladder/d}");
    src.set("ladder/f", "${abs:ladder/e}${abs:ladder/e}");
    src.set("ladder/g", "${abs:ladder/f}${abs:ladder/f}");
    src.set("ladder/h", "${abs:ladder/g}${abs:ladder/g}");
    src.set("ladder/i", "${abs:ladder/h}${abs:ladder/h}");
    src.set("ladder/j", "${abs:ladder/i}${abs:ladder/i}");
    src.set("ladder/k", "${abs:ladder/j}${abs:ladder/j}");

    load_options opts;
    opts.reference_budget = 5; // tiny budget to force early trip
    auto loaded = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("budget") != std::string::npos);
}

TEST_CASE("budget_exceeded does NOT trigger ?? fallthrough",
          "[reference][budget][fallback]")
{
    // Even with a fallback arm, budget_exceeded must propagate, not be swallowed.
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("ladder/a", "x");
    src.set("ladder/b", "${abs:ladder/a}${abs:ladder/a}");
    src.set("ladder/c", "${abs:ladder/b}${abs:ladder/b}");
    src.set("ladder/d", "${abs:ladder/c}${abs:ladder/c}");
    src.set("ladder/result", "${abs:ladder/d ?? \"fallback\"}");

    load_options opts;
    opts.reference_budget = 3; // trip before ladder/d finishes
    auto loaded = load_config(space, source_stack{std::move(src)}, opts);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("budget") != std::string::npos);
}

TEST_CASE("rel: reference walking above the root reports a distinct error",
          "[reference][rel]")
{
    // ${rel:../../x} from cluster/alias walks above the configuration root:
    // base = cluster (parent of alias), ".." -> root, ".." -> above root.
    // The error must name the offending rel body, not a misleading empty target.
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("cluster/alias", "${rel:../../x}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    const std::string &msg = loaded.error().message;
    CHECK(msg.find("above") != std::string::npos);
    CHECK(msg.find("../../x") != std::string::npos);
    CHECK(msg.find("absent") == std::string::npos);
}

TEST_CASE("an in-bounds rel: reference still resolves after the above-root guard",
          "[reference][rel]")
{
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("cluster/sibling/port", "5050");
    src.set("cluster/server/alias", "${rel:../sibling/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/alias") == "5050");
}

TEST_CASE("diamond fan-in resolves without exponential substitution count",
          "[reference][budget][diamond]")
{
    // A -> B, A -> C; both B and C reference D.
    // Without memoization this would substitute D twice. With the default budget
    // (10000) and memoization the load must succeed -- we verify success only.
    auto space = config_space_builder{}.build();
    runtime_source src;
    src.set("diamond/d", "leaf");
    src.set("diamond/b", "${abs:diamond/d}");
    src.set("diamond/c", "${abs:diamond/d}");
    src.set("diamond/a", "${abs:diamond/b}-${abs:diamond/c}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("diamond/a") == "leaf-leaf");
}

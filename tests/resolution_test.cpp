#include "nucleus/nucleus.h"
#include "nucleus/identity.h"
#include "nucleus/entry/precedence.h"
#include "nucleus/source/env/env_source.h"
#include "nucleus/source/argv/argv_source.h"

#include "nucleus/schema/schema_registry.h"
#include "nucleus/source/source_registry.h"
#include "nucleus/entry/resolution_context.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/builtin_tokenizers.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// The facade-level convergence: a transient resolution_context borrows the
// registries, folds a precedence stack with provenance, and freezes an immutable
// configuration -- with the two-phase state machine enforced around it.

TEST_CASE("resolve folds a precedence stack and freezes an immutable result", "[resolution]")
{
    nucleus::nucleus engine;

    nucleus::env_source defaults;
    defaults.set("server/host", "localhost").set("server/port", "80");

    nucleus::env_source env;
    env.set("server/port", "8080"); // overrides the default port

    nucleus::source_stack stack;
    stack.add(defaults, nucleus::layer_rank::defaults, "defaults");
    stack.add(env, nucleus::layer_rank::env, "env");

    REQUIRE(engine.phase() == nucleus::facade_phase::configurable);

    auto loaded = engine.resolve(stack);
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Higher precedence (env) won the port; the un-contested host survives.
    REQUIRE(config.get("server/host") == "localhost");
    REQUIRE(config.get("server/port") == "8080");

    // The facade is now resolved.
    REQUIRE(engine.phase() == nucleus::facade_phase::resolved);
}

TEST_CASE("value and provenance are recorded in the same fold and cannot diverge", "[resolution]")
{
    nucleus::nucleus engine;

    nucleus::env_source base;
    base.set("a", "from-base").set("b", "base-only");

    nucleus::env_source over;
    over.set("a", "from-overlay");

    nucleus::source_stack stack;
    stack.add(base, nucleus::layer_rank::base, "base");
    stack.add(over, nucleus::layer_rank::overlay, "overlay");

    auto loaded = engine.resolve(stack);
    REQUIRE(loaded);
    const auto &config = loaded.value();

    REQUIRE(config.get("a") == "from-overlay");
    REQUIRE(config.get("b") == "base-only");

    // The winning value's provenance names the layer that actually set it.
    const nucleus::origin *a_origin = config.provenance_of("a");
    REQUIRE(a_origin != nullptr);
    REQUIRE(a_origin->layer == "overlay");

    const nucleus::origin *b_origin = config.provenance_of("b");
    REQUIRE(b_origin != nullptr);
    REQUIRE(b_origin->layer == "base");
}

TEST_CASE("explicit precedence is argv over overlay over base over env over defaults", "[resolution]")
{
    nucleus::nucleus engine;

    nucleus::env_source defaults;  defaults.set("k", "defaults");
    nucleus::env_source env;       env.set("k", "env");
    nucleus::env_source base;      base.set("k", "base");
    nucleus::env_source overlay;   overlay.set("k", "overlay");
    nucleus::argv_source argv(std::vector<std::string>{"--k=argv"});

    // Added out of rank order on purpose: the fold sorts by rank, not arrival.
    nucleus::source_stack stack;
    stack.add(overlay,  nucleus::layer_rank::overlay,  "overlay");
    stack.add(defaults, nucleus::layer_rank::defaults, "defaults");
    stack.add(argv,     nucleus::layer_rank::argv,     "argv");
    stack.add(env,      nucleus::layer_rank::env,      "env");
    stack.add(base,     nucleus::layer_rank::base,     "base");

    auto loaded = engine.resolve(stack);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("k") == "argv");
    REQUIRE(loaded.value().provenance_of("k")->layer == "argv");
}

TEST_CASE("registration after resolve is a state-machine error", "[resolution][lifecycle]")
{
    nucleus::nucleus engine;
    nucleus::env_source one; one.set("k", "v");
    nucleus::source_stack stack;
    stack.add(one, nucleus::layer_rank::base, "base");

    REQUIRE(engine.resolve(stack));

    auto reg = engine.register_schema("late");
    REQUIRE_FALSE(reg);
    REQUIRE(reg.error().find("resolved") != std::string::npos);

    // A second resolve is likewise refused by the state machine.
    auto again = engine.resolve(stack);
    REQUIRE_FALSE(again);
    REQUIRE(again.error().find("already resolved") != std::string::npos);
}

TEST_CASE("the args-only overload wires the argv recognizer to the schema", "[resolution][lifecycle]")
{
    SECTION("a declared flag resolves")
    {
        nucleus::nucleus engine;
        REQUIRE(engine.register_schema("logging/level"));

        auto loaded = engine.load(std::vector<std::string>{"--logging-level=debug"});
        REQUIRE(loaded);
        REQUIRE(loaded.value().get("logging/level") == "debug");
    }

    SECTION("an undeclared flag is rejected by the schema authority")
    {
        nucleus::nucleus engine;
        REQUIRE(engine.register_schema("logging/level"));

        auto loaded = engine.load(std::vector<std::string>{"--logging-levle=debug"});
        REQUIRE_FALSE(loaded);
        REQUIRE(loaded.error().find("logging/levle") != std::string::npos);
    }
}

TEST_CASE("an unresolvable token fails the fold loudly rather than passing through", "[resolution][tokens]")
{
    nucleus::nucleus engine;

    nucleus::env_source env;
    env.set("greeting", "${string.upper(hi)}");

    nucleus::source_stack stack;
    stack.add(env, nucleus::layer_rank::base, "base");

    // The facade's default tokenizer registry has no `string` category, so the
    // ${...} cannot resolve: the fold reports the offending key instead of
    // silently layering the unexpanded text.
    auto loaded = engine.resolve(stack);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("greeting") != std::string::npos);
}

TEST_CASE("tokens are expanded per-source before layering (expand-then-layer)", "[resolution][tokens]")
{
    // Driven at the keystone directly so a real tokenizer registry is in scope:
    // the three registries are built independently and meet only through the
    // transient borrowing context.
    nucleus::schema_registry schema;
    nucleus::tokenizer_registry tokenizer;
    nucleus::source_registry sources;
    tokenizer.add(nucleus::make_string_tokenizer(), nucleus::owner_token{});

    nucleus::env_source env;
    env.set("loud", "${string.upper(hi)}").set("plain", "kept");

    nucleus::source_stack stack;
    stack.add(env, nucleus::layer_rank::base, "base");

    nucleus::resolution_context ctx(schema, tokenizer, sources);
    auto folded = ctx.fold(stack);
    REQUIRE(folded);

    nucleus::configuration config = ctx.freeze();
    // The token expanded at read time; the layered value is already resolved.
    REQUIRE(config.get("loud") == "HI");
    REQUIRE(config.get("plain") == "kept");
}

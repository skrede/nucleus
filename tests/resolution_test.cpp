#include "nucleus/configuration_space.h"
#include "nucleus/identity.h"

#include "nucleus/configuration_source/env/env_source.h"
#include "nucleus/configuration_source/argv/argv_source.h"

#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/builtin_tokenizers.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <string_view>

// The convergence: a sealed configuration_space, a per-load source_stack,
// and the free load that folds a precedence stack with provenance and
// freezes an immutable configuration via a stack-local, const-borrowing context.

TEST_CASE("resolve folds a precedence stack and freezes an immutable result", "[resolution]")
{
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source defaults;
    defaults.set("server/host", "localhost").set("server/port", "80");

    nucleus::env_source env;
    env.set("server/port", "8080"); // overrides the default port

    // defaults at lower precedence (stack[0]), env at higher precedence (stack[1]).
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(defaults), std::move(env)},
        {});
    REQUIRE(loaded);
    const nucleus::configuration &config = loaded.value();

    // Higher precedence (env) won the port; the un-contested host survives.
    REQUIRE(config.get("server/host") == "localhost");
    REQUIRE(config.get("server/port") == "8080");
}

TEST_CASE("value and provenance are recorded in the same fold and cannot diverge", "[resolution]")
{
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source base;
    base.set("a", "from-base").set("b", "base-only");

    nucleus::env_source over;
    over.set("a", "from-overlay");

    // base at lower precedence (stack[0]), over at higher precedence (stack[1]).
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(base), std::move(over)},
        {});
    REQUIRE(loaded);
    const auto &config = loaded.value();

    REQUIRE(config.get("a") == "from-overlay");
    REQUIRE(config.get("b") == "base-only");

    // The winning value's provenance names the layer that actually set it.
    const nucleus::origin *a_origin = config.provenance_of("a");
    REQUIRE(a_origin != nullptr);
    REQUIRE(a_origin->layer == "stack[1]");

    const nucleus::origin *b_origin = config.provenance_of("b");
    REQUIRE(b_origin != nullptr);
    REQUIRE(b_origin->layer == "stack[0]");
}

TEST_CASE("explicit precedence is argv over overlay over base over env over defaults", "[resolution]")
{
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source defaults;  defaults.set("k", "defaults");
    nucleus::env_source env;       env.set("k", "env");
    nucleus::env_source base;      base.set("k", "base");
    nucleus::env_source overlay;   overlay.set("k", "overlay");
    nucleus::argv_source argv(std::vector<std::string>{"--k=argv"});

    // Listed in ascending-precedence order: defaults(stack[0]) < env(stack[1])
    // < base(stack[2]) < overlay(stack[3]) < argv(stack[4]).
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(defaults), std::move(env),
                              std::move(base), std::move(overlay), std::move(argv)},
        {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("k") == "argv");
    REQUIRE(loaded.value().provenance_of("k")->layer == "stack[4]");
}

TEST_CASE("a sealed space loads repeatedly and a built builder rejects registration",
          "[resolution][lifecycle]")
{
    nucleus::configuration_space_builder engine;
    nucleus::configuration_space space = engine.build();

    nucleus::env_source one; one.set("k", "v");

    // Unlike the old one-shot facade, the sealed space serves repeated loads: it is
    // immutable and load owns all mutable state on its own stack.
    auto first = nucleus::load(space, nucleus::source_stack{one}, {});
    REQUIRE(first);
    auto second = nucleus::load(space, nucleus::source_stack{one}, {});
    REQUIRE(second);
    REQUIRE(second.value().get("k") == "v");

    // Registering on an already-built builder is a loud state-machine error.
    auto reg = engine.register_schema("late");
    REQUIRE_FALSE(reg);
    REQUIRE(reg.error().find("already been built") != std::string::npos);
}

TEST_CASE("the args-only options wire the argv recognizer to the schema", "[resolution][lifecycle]")
{
    SECTION("a declared flag resolves")
    {
        nucleus::configuration_space_builder engine;
        REQUIRE(engine.register_schema("logging/level"));
        nucleus::configuration_space space = engine.build();

        nucleus::argv_source argv(std::vector<std::string>{"--logging-level=debug"});
        argv.recognize_with(nucleus::recognizer_of(space));
        auto loaded = nucleus::load(space, nucleus::source_stack{std::move(argv)}, {});
        REQUIRE(loaded);
        REQUIRE(loaded.value().get("logging/level") == "debug");
    }

    SECTION("an undeclared flag is rejected by the schema authority")
    {
        nucleus::configuration_space_builder engine;
        REQUIRE(engine.register_schema("logging/level"));
        nucleus::configuration_space space = engine.build();

        nucleus::argv_source argv(std::vector<std::string>{"--logging-levle=debug"});
        argv.recognize_with(nucleus::recognizer_of(space));
        auto loaded = nucleus::load(space, nucleus::source_stack{std::move(argv)}, {});
        REQUIRE_FALSE(loaded);
        REQUIRE(loaded.error().find("logging/levle") != std::string::npos);
    }
}

TEST_CASE("a later-listed stack source wins a same-key contest against an earlier one",
          "[resolution][precedence]")
{
    // Two sources contesting the same key: later index == higher rank == wins.
    // The document-band clamping (200-900) ensures document paths similarly
    // yield to a stack source placed after them; the "last-wins" rule applies
    // uniformly whether the contenders are flat sources or document paths.
    nucleus::configuration_space_builder engine;
    REQUIRE(engine.register_schema("k"));
    nucleus::configuration_space space = engine.build();

    nucleus::env_source lower; lower.set("k", "from-lower");
    nucleus::env_source higher; higher.set("k", "from-higher");

    // lower at stack[0] (rank 0), higher at stack[1] (rank 1): higher wins.
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(lower), std::move(higher)},
        {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("k") == "from-higher");
    REQUIRE(loaded.value().provenance_of("k")->layer == "stack[1]");
}

TEST_CASE("the last config document wins among layered paths", "[resolution][precedence]")
{
    // No argv: the last path in the list must still win over earlier ones even
    // though all documents past the base are clamped to the same band.
    auto make = [](const std::string &path) -> nucleus::source_handle {
        nucleus::env_source src;
        src.set("k", path);
        return nucleus::source_handle(std::move(src));
    };

    nucleus::configuration_space_builder engine;
    REQUIRE(engine.register_schema("k"));
    nucleus::configuration_space space = engine.build();

    auto loaded = nucleus::load(space, nucleus::source_stack{},
        nucleus::load_options{.document_paths = {"first", "second", "third", "fourth"},
                              .make_document = make});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("k") == "fourth");
}

TEST_CASE("an unresolvable token fails the fold loudly rather than passing through", "[resolution][tokens]")
{
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source env;
    // No tokenizer answers the `nope` category (the core builtins are
    // env/string), so the ${...} cannot resolve.
    env.set("greeting", "${nope.whatever}");

    // The fold reports the offending key instead of silently layering the
    // unexpanded text.
    auto loaded = nucleus::load(space, nucleus::source_stack{std::move(env)}, {});
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().find("greeting") != std::string::npos);
}

TEST_CASE("the space resolves core builtin tokens with no extra registration", "[resolution][tokens]")
{
    // A host that registers nothing special must still get token expansion: the
    // generic core tokenizers are installed by default on the builder.
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source env;
    env.set("greeting", "${string.upper(hi)}");
    env.set("token", "${string.concat(a,b,c)}");

    auto loaded = nucleus::load(space, nucleus::source_stack{std::move(env)}, {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("greeting") == "HI");
    REQUIRE(loaded.value().get("token") == "abc");
}

TEST_CASE("install_tokenizer injects an additional tokenizer reachable at resolve", "[resolution][tokens]")
{
    nucleus::configuration_space_builder engine;

    // A host-built tokenizer for a custom category, installed through the builder.
    nucleus::tokenizer_builder builder("greet");
    builder.set_wildcard([](std::string_view who) -> nucleus::token_result {
        return std::string("hello ") + std::string(who);
    });
    REQUIRE(engine.install_tokenizer(std::move(builder).build()));
    nucleus::configuration_space space = engine.build();

    nucleus::env_source env;
    env.set("msg", "${greet.world}");

    auto loaded = nucleus::load(space, nucleus::source_stack{std::move(env)}, {});
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("msg") == "hello world");
}

TEST_CASE("tokens are expanded per-source before layering (expand-then-layer)", "[resolution][tokens]")
{
    // A single env source carrying one token and one plain value: the token must
    // be expanded at fold time so the frozen configuration holds the resolved form.
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source env;
    env.set("loud", "${string.upper(hi)}").set("plain", "kept");

    auto loaded = nucleus::load(space, nucleus::source_stack{std::move(env)}, {});
    REQUIRE(loaded);

    const nucleus::configuration &config = loaded.value();
    // The token expanded at read time; the layered value is already resolved.
    REQUIRE(config.get("loud") == "HI");
    REQUIRE(config.get("plain") == "kept");
}

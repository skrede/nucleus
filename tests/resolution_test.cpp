#include "nucleus/configuration_space.h"
#include "nucleus/identity.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/resolution_context.h"

#include "nucleus/configuration_source/env/env_source.h"

#include "nucleus/schema/schema_registry.h"

#include "nucleus/configuration_source/configuration_source_registry.h"

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

// The convergence: a sealed configuration_space, a per-load source_stack_options,
// and the free load_configuration that folds a precedence stack with provenance and
// freezes an immutable configuration via a stack-local, const-borrowing context.

namespace {

// Borrows one source at an explicit rank through the per-load options.
nucleus::source_stack_options layer_at(nucleus::configuration_source &src,
                                       nucleus::layer_rank rank, std::string label)
{
    nucleus::source_stack_options opts;
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &src, static_cast<std::size_t>(rank), std::move(label), {}});
    return opts;
}

} // namespace

TEST_CASE("resolve folds a precedence stack and freezes an immutable result", "[resolution]")
{
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source defaults;
    defaults.set("server/host", "localhost").set("server/port", "80");

    nucleus::env_source env;
    env.set("server/port", "8080"); // overrides the default port

    nucleus::source_stack_options opts;
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &defaults, static_cast<std::size_t>(nucleus::layer_rank::defaults), "defaults", {}});
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &env, static_cast<std::size_t>(nucleus::layer_rank::env), "env", {}});

    auto loaded = nucleus::load_configuration(space, opts);
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

    nucleus::source_stack_options opts;
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &base, static_cast<std::size_t>(nucleus::layer_rank::base), "base", {}});
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &over, static_cast<std::size_t>(nucleus::layer_rank::overlay), "overlay", {}});

    auto loaded = nucleus::load_configuration(space, opts);
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
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::env_source defaults;  defaults.set("k", "defaults");
    nucleus::env_source env;       env.set("k", "env");
    nucleus::env_source base;      base.set("k", "base");
    nucleus::env_source overlay;   overlay.set("k", "overlay");
    nucleus::argv_source argv(std::vector<std::string>{"--k=argv"});

    // Added out of rank order on purpose: the fold sorts by rank, not arrival.
    nucleus::source_stack_options opts;
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &overlay, static_cast<std::size_t>(nucleus::layer_rank::overlay), "overlay", {}});
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &defaults, static_cast<std::size_t>(nucleus::layer_rank::defaults), "defaults", {}});
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &argv, static_cast<std::size_t>(nucleus::layer_rank::argv), "argv", {}});
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &env, static_cast<std::size_t>(nucleus::layer_rank::env), "env", {}});
    opts.custom_layers.push_back(nucleus::configuration_source_layer{
        &base, static_cast<std::size_t>(nucleus::layer_rank::base), "base", {}});

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("k") == "argv");
    REQUIRE(loaded.value().provenance_of("k")->layer == "argv");
}

TEST_CASE("a sealed space loads repeatedly and a built builder rejects registration",
          "[resolution][lifecycle]")
{
    nucleus::configuration_space_builder engine;
    nucleus::configuration_space space = engine.build();

    nucleus::env_source one; one.set("k", "v");
    nucleus::source_stack_options opts = layer_at(one, nucleus::layer_rank::base, "base");

    // Unlike the old one-shot facade, the sealed space serves repeated loads: it is
    // immutable and load_configuration owns all mutable state on its own stack.
    auto first = nucleus::load_configuration(space, opts);
    REQUIRE(first);
    auto second = nucleus::load_configuration(space, opts);
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

        nucleus::source_stack_options opts;
        opts.argv = nucleus::argv_source_options{{"--logging-level=debug"}};

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE(loaded);
        REQUIRE(loaded.value().get("logging/level") == "debug");
    }

    SECTION("an undeclared flag is rejected by the schema authority")
    {
        nucleus::configuration_space_builder engine;
        REQUIRE(engine.register_schema("logging/level"));
        nucleus::configuration_space space = engine.build();

        nucleus::source_stack_options opts;
        opts.argv = nucleus::argv_source_options{{"--logging-levle=debug"}};

        auto loaded = nucleus::load_configuration(space, opts);
        REQUIRE_FALSE(loaded);
        REQUIRE(loaded.error().find("logging/levle") != std::string::npos);
    }
}

TEST_CASE("argv outranks any number of config documents", "[resolution][precedence]")
{
    // Each path becomes a one-key source whose value is the path label, so the
    // winning value names which layer won. With four-plus documents a naive
    // base+index rank would push a document to or past the argv rank; the clamp
    // keeps every document strictly below argv.
    auto make = [](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        auto src = std::make_unique<nucleus::env_source>();
        src->set("k", path);
        return src;
    };

    nucleus::configuration_space_builder engine;
    REQUIRE(engine.register_schema("k"));
    nucleus::configuration_space space = engine.build();

    nucleus::source_stack_options opts;
    opts.argv = nucleus::argv_source_options{{"--k=from-argv"}};
    opts.document_paths = {"p0", "p1", "p2", "p3", "p4"};
    opts.make_document = make;

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("k") == "from-argv");
    REQUIRE(loaded.value().provenance_of("k")->layer == "argv");
}

TEST_CASE("the last config document wins among layered paths", "[resolution][precedence]")
{
    // No argv: the last path in the list must still win over earlier ones even
    // though all documents past the base are clamped to the same band.
    auto make = [](const std::string &path) -> std::unique_ptr<nucleus::configuration_source> {
        auto src = std::make_unique<nucleus::env_source>();
        src->set("k", path);
        return src;
    };

    nucleus::configuration_space_builder engine;
    REQUIRE(engine.register_schema("k"));
    nucleus::configuration_space space = engine.build();

    nucleus::source_stack_options opts;
    opts.document_paths = {"first", "second", "third", "fourth"};
    opts.make_document = make;

    auto loaded = nucleus::load_configuration(space, opts);
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

    nucleus::source_stack_options opts = layer_at(env, nucleus::layer_rank::base, "base");

    // The fold reports the offending key instead of silently layering the
    // unexpanded text.
    auto loaded = nucleus::load_configuration(space, opts);
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

    nucleus::source_stack_options opts = layer_at(env, nucleus::layer_rank::base, "base");

    auto loaded = nucleus::load_configuration(space, opts);
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

    nucleus::source_stack_options opts = layer_at(env, nucleus::layer_rank::base, "base");

    auto loaded = nucleus::load_configuration(space, opts);
    REQUIRE(loaded);
    REQUIRE(loaded.value().get("msg") == "hello world");
}

TEST_CASE("tokens are expanded per-source before layering (expand-then-layer)", "[resolution][tokens]")
{
    // Driven at the keystone directly so a real tokenizer registry is in scope:
    // the borrowed registries are built independently and meet only through the
    // transient borrowing context.
    nucleus::schema_registry schema;
    nucleus::tokenizer_registry tokenizer;
    tokenizer.add(nucleus::make_string_tokenizer(), nucleus::owner_token{});
    nucleus::converter_registry converters;

    nucleus::env_source env;
    env.set("loud", "${string.upper(hi)}").set("plain", "kept");

    nucleus::configuration_source_stack stack;
    stack.add(env, nucleus::layer_rank::base, "base");

    nucleus::resolution_context ctx(schema, tokenizer, converters);
    auto folded = ctx.fold(stack);
    REQUIRE(folded);

    nucleus::configuration config = ctx.freeze();
    // The token expanded at read time; the layered value is already resolved.
    REQUIRE(config.get("loud") == "HI");
    REQUIRE(config.get("plain") == "kept");
}

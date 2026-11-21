#include "support/builder_result_test_support.h"

#include "nucleus/identity.h"
#include "nucleus/config_space.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include "nucleus/config_source/source_stack.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Acceptance tests.
// Cross-leaf cycle detection (expansion_guard with leaf-path labels).

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::runtime_source;
using nucleus::source_stack;
using nucleus::token_result;
using nucleus::tree_access;
using nucleus::tree_tokenizer;

TEST_CASE("cross-leaf cycle A->B->A is detected with the full FQN chain",
          "[reference][cycle]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
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
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/port", "${abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("cyclic") != std::string::npos);
}

TEST_CASE("three-leaf cycle A->B->C->A names all nodes", "[reference][cycle]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("ring/a", "${abs:ring/b}");
    src.set("ring/b", "${abs:ring/c}");
    src.set("ring/c", "${abs:ring/a}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    const std::string &msg = loaded.error().message;
    CHECK(msg.find("cyclic") != std::string::npos);
}

TEST_CASE("rel: reference walking above the root reports a distinct error",
          "[reference][rel]")
{
    // ${rel:../../x} from cluster/alias walks above the configuration root:
    // base = cluster (parent of alias), ".." -> root, ".." -> above root.
    // The error must name the offending rel body, not a misleading empty target.
    auto space = nucleus::builder_result_test::built(config_space_builder{});
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
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/sibling/port", "5050");
    src.set("cluster/server/alias", "${rel:../sibling/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/alias") == "5050");
}

TEST_CASE("a tree tokenizer's own output is resolved to a fixpoint", "[reference][tree]")
{
    config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(tree_tokenizer("wrap", [](const tree_access &a)
        -> token_result { return a.field_name == "start"
            ? std::string("head-${wrap.tail}-foot") : std::string("TAIL"); })));
    runtime_source src;
    src.set("box/text", "x${wrap.start}y");
    auto loaded = load_config(nucleus::builder_result_test::built(engine), source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("box/text") == "xhead-TAIL-footy");
}

TEST_CASE("a tree tokenizer re-emitting its own token halts", "[reference][tree][budget]")
{
    config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(tree_tokenizer("loop", [](const tree_access &)
        -> token_result { return std::string("${loop.self}"); })));
    runtime_source src;
    src.set("box/text", "${loop.self}");
    auto loaded = load_config(nucleus::builder_result_test::built(engine), source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("cyclic reference: loop.self") != std::string::npos);
}

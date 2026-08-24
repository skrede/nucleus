#include "builder_result_test_support.h"
#include "nucleus/identity.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstddef>

// Acceptance tests.
// All tests drive the public load_config() pipeline with a runtime_source so the
// full fold -> slice -> resolve_references() -> validate() -> freeze() chain runs.

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::load_options;
using nucleus::runtime_source;
using nucleus::source_stack;

TEST_CASE("abs: reference resolves a value by absolute path", "[reference][abs]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/port", "9090");
    src.set("cluster/alias", "${abs:cluster/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias") == "9090");
}

TEST_CASE("abs: reference to non-existent path is a hard error", "[reference][abs]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/alias", "${abs:cluster/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("absent") != std::string::npos);
}

TEST_CASE("rel: child reference descends from containing scope", "[reference][rel]")
{
    // From cluster/alias the base is cluster, so a bare name descends from there.
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/port", "8080");
    src.set("cluster/alias", "${rel:port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias") == "8080");
}

TEST_CASE("rel: ../ reference walks up then descends", "[reference][rel]")
{
    // From cluster/server/alias the base is cluster/server, ".." reaches cluster,
    // and the remaining segments descend to cluster/sibling/port.
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/sibling/port", "7070");
    src.set("cluster/server/alias", "${rel:../sibling/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/server/alias") == "7070");
}

TEST_CASE("rel: ./ is sugar for current-scope descend", "[reference][rel]")
{
    // From cluster/alias the base is cluster, "." moves nothing, "port" descends.
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/port", "6060");
    src.set("cluster/alias", "${rel:./port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias") == "6060");
}

TEST_CASE("abs: reference including indexed segment resolves correctly", "[reference][abs]")
{
    // Indexed paths are supplied directly, as a tree source would emit them, so the
    // case carries no schema dependency.
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/node[0]/name", "primary");
    src.set("cluster/node[1]/name", "secondary");
    src.set("cluster/primary_name", "${abs:cluster/node[0]/name}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/primary_name") == "primary");
}

TEST_CASE("resolve_references() runs post-slice, not during fold", "[reference][pipeline]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/port", "5050");
    src.set("host/display", "${abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/display") == "5050");
}

TEST_CASE("value-only invariant: reference in key position is a loud error",
          "[reference][value-only]")
{
    // key_path::parse would refuse "${" in most positions, but a source can emit a
    // path carrying that substring; the invariant fires before any resolution.
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/${abs:x}/port", "1234");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("structural key position") != std::string::npos);
}

TEST_CASE("multiple references in one value string all resolve", "[reference][abs]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("host/name", "myhost");
    src.set("host/port", "8080");
    src.set("host/addr", "${abs:host/name}:${abs:host/port}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("host/addr") == "myhost:8080");
}

TEST_CASE("a closing brace inside a quoted fallback arm does not end the tree token",
          "[reference][quoting]")
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/alias", "${abs:cluster/absent ?? \"}\"}");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get("cluster/alias").value() == "}");
}

// Leaf `pool/lN` nests six dispatches (`d.lNn5` down to `d.lNn0`) and then hops to
// `pool/lN+1`, so the dispatch nesting the load reaches is the sum over the chain.
static std::string dispatch_chain(std::size_t leaves)
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(nucleus::tree_tokenizer("d",
        [](const nucleus::tree_access &a) -> nucleus::token_result {
            if(a.field_name[3] != '0')
                return std::string("${d.l") + a.field_name[1] + "n" + char(a.field_name[3] - 1) + "}";
            return std::string("${abs:pool/l") + char(a.field_name[1] + 1) + "}";
        })));
    runtime_source src;
    for(std::size_t leaf = 0; leaf < leaves; ++leaf)
        src.set("pool/l" + std::to_string(leaf), "${d.l" + std::to_string(leaf) + "n5}");
    src.set("pool/l" + std::to_string(leaves), "END");
    auto loaded = load_config(nucleus::builder_result_test::built(engine),
                              source_stack{std::move(src)}, {});
    return loaded ? loaded.value().get("pool/l0").value() : loaded.error().message;
}

TEST_CASE("a leaf hop does not reset the tokenizer-dispatch chain",
          "[reference][tree][depth]")
{
    CHECK(dispatch_chain(2) == "END");
    CHECK(dispatch_chain(4).find("depth") != std::string::npos);
}

static std::string produced(const std::string &text)
{
    nucleus::config_space_builder engine;
    REQUIRE(engine.install_tree_tokenizer(nucleus::tree_tokenizer("emit",
        [text](const nucleus::tree_access &) -> nucleus::token_result { return text; })));
    runtime_source src;
    src.set("a/v", "${emit.text}");
    auto loaded = load_config(nucleus::builder_result_test::built(engine),
                              source_stack{std::move(src)}, {});
    return loaded ? loaded.value().get("a/v").value() : loaded.error().message;
}

TEST_CASE("a resolver's output is expanded or reported, never rewritten",
          "[reference][tree][produced]")
{
    CHECK(produced("plain") == "plain");
    CHECK(produced("PREFIX=${HOME}/bin").find("unrecognized token body 'HOME'")
          != std::string::npos);
    CHECK(produced("${\"PREFIX=${HOME}/bin\"}") == "PREFIX=${HOME}/bin");
    CHECK(produced("literal ${ brace").find("unterminated") != std::string::npos);
    CHECK(produced("${abs:a/missing ?? fallback}") == "fallback");
}

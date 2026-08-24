#include "nucleus/query/query.h"
#include "builder_result_test_support.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

// Lifetime contract tests for the query/selector API.
//
// The [.][asan] test is hidden from normal ctest runs and must be executed
// explicitly under -fsanitize=address. It is expected to trigger a heap-use-
// after-free, proving the sanitizer is wired and that config_node results carry
// a dangling pointer once the config is destroyed.
//
// The schema_query_context lifetime test always passes; it serves as executable
// documentation that a selector owns its context by value and so may be built
// from a temporary schema_query_context that is gone before the query runs.

using namespace nucleus;

namespace {

config_space make_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(element("port",  anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(element("host",  anchor::keyspace("cluster"))));
    return nucleus::builder_result_test::built(std::move(builder));
}

config make_config(const config_space &space)
{
    runtime_source src;
    src.set("cluster/port", "8080");
    src.set("cluster/host", "localhost");
    auto res = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(res.has_value());
    return std::move(*res);
}

}

// -------------------------------------------------------------------------
// Hidden ASan guard — run explicitly: ./selector_lifetime_test [asan]
// Expected to trip heap-use-after-free when ASan is enabled.
//
// The test deliberately reads through a dangling config_node after the config
// has been destroyed, mirroring the buffer_drop pattern from
// tests/system_stack_buffer_drop_test.cpp.
// -------------------------------------------------------------------------

// NOLINTNEXTLINE(cert-err58-cpp): expected UB — ASan trip test
TEST_CASE("lifetime: results must not outlive the config (ASan guard)",
          "[.][asan]")
{
    const auto space = make_space();
    const auto ctx   = space.query_context();

    std::vector<config_node> results;
    {
        auto cfg = make_config(space);
        results = query(cfg.root(), ctx).leaves().collect();
        // cfg is destroyed here — results now hold dangling config pointers.
    }

    // Reading through a dangling pointer after destruction.
    // Under ASan this must trigger heap-use-after-free.
    // In non-ASan builds this is UB that may or may not fault.
    (void)results[0].value();
}

// -------------------------------------------------------------------------
// A selector owns its schema_query_context by value, so it can be built from a
// TEMPORARY query_context() and used after the full expression ends. The old
// pointer-storing selector could not survive this — the context dangled.
// -------------------------------------------------------------------------

TEST_CASE("lifetime: selector survives a temporary schema_query_context",
          "[selector][lifetime]")
{
    const auto space = make_space();
    const auto cfg   = make_config(space);

    // space.query_context() is a temporary; the selector copies it in by value,
    // so it is safe to use after the temporary has been destroyed.
    auto s = query(cfg.root(), space.query_context());

    auto nodes = s.leaves().collect();
    CHECK_FALSE(nodes.empty());

    // or_() captures a context copy, so a union of two temporaries is safe too.
    auto both = query(cfg.root(), space.query_context()).leaves()
                    .or_(query(cfg.root(), space.query_context()).containers())
                    .collect();
    CHECK_FALSE(both.empty());
}

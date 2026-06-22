#include "nucleus/query/query.h"
#include "nucleus/config_space.h"
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
// documentation of the ctx-must-not-outlive-space contract.

using namespace nucleus;

namespace {

config_space make_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(element("port",  anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(element("host",  anchor::keyspace("cluster"))));
    return std::move(builder).build();
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
// Always-passing documentation test: ctx-must-not-outlive-space contract.
//
// The schema_query_context borrows the config_space. The correct usage
// demonstrated in all selector tests is to keep both alive for the duration
// of the query, then let them go out of scope together. This test formalises
// that contract as executable documentation.
// -------------------------------------------------------------------------

TEST_CASE("lifetime: schema_query_context must not outlive config_space (contract doc)",
          "[selector][lifetime]")
{
    // Correct usage: ctx is always destroyed before or with space.
    const auto space = make_space();
    {
        const auto ctx = space.query_context();
        const auto cfg = make_config(space);

        // ctx, cfg, space are all valid here — query is safe.
        auto nodes = query(cfg.root(), ctx).leaves().collect();
        CHECK_FALSE(nodes.empty());
    }
    // ctx and cfg are gone; space is still alive — correct tear-down order.
    // No assertion needed: the fact that this compiles and runs without UB
    // documents the expected lifetime order.
    SUCCEED("Correct lifetime order: ctx and cfg destroyed before space");
}

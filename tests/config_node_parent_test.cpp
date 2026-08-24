#include "nucleus/config_node.h"
#include "builder_result_test_support.h"
#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// Acceptance tests: config_node::parent() and config_node::ancestor().
// Covers root/single/multi-segment paths, indexed segments, named ancestor
// matching, and no-match returns.

using nucleus::config_space_builder;
using nucleus::load_config;
using nucleus::runtime_source;
using nucleus::source_stack;

namespace {

nucleus::config load_flat()
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/server/port", "8080");
    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    return std::move(loaded).value();
}

nucleus::config load_indexed()
{
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    runtime_source src;
    src.set("cluster/node[0]/port", "9090");
    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(loaded.has_value());
    return std::move(loaded).value();
}

}

TEST_CASE("parent() on multi-segment path strips last segment", "[config_node][parent]")
{
    const auto cfg = load_flat();
    const auto port_node = cfg.root()["cluster"]["server"]["port"];
    REQUIRE(port_node.exists());

    const auto p = port_node.parent();
    REQUIRE(p.exists());
    CHECK(p.path() == "cluster/server");
}

TEST_CASE("parent() on single-segment path returns root node", "[config_node][parent]")
{
    const auto cfg = load_flat();
    // "cluster" is a single-segment path; its parent is the root (empty path).
    const auto cluster_node = cfg.root()["cluster"];
    REQUIRE(cluster_node.exists());

    const auto p = cluster_node.parent();
    // Root node has empty path and exists when the config has keys.
    REQUIRE(p.exists());
    CHECK(p.path() == "");
}

TEST_CASE("parent() of root returns a null node", "[config_node][parent]")
{
    const auto cfg = load_flat();
    const auto root = cfg.root();
    // The root node has an empty path; parent() of root must return null.
    const auto p = root.parent();
    CHECK_FALSE(p.exists());
}

TEST_CASE("parent() of indexed path cluster/node[0] returns cluster", "[config_node][parent]")
{
    const auto cfg = load_indexed();
    // Navigate to the indexed container node[0].
    const auto node0 = cfg.root()["cluster"]["node"][std::size_t{0}];
    REQUIRE(node0.exists());

    // parent() of "cluster/node[0]" strips the last segment ("node[0]"), leaving "cluster".
    const auto p = node0.parent();
    REQUIRE(p.exists());
    CHECK(p.path() == "cluster");
}

TEST_CASE("ancestor() walks toward root and matches base name", "[config_node][ancestor]")
{
    const auto cfg = load_flat();
    const auto port_node = cfg.root()["cluster"]["server"]["port"];
    REQUIRE(port_node.exists());

    // "cluster" is an ancestor of "cluster/server/port".
    const auto anc = port_node.ancestor("cluster");
    REQUIRE(anc.exists());
    CHECK(anc.path() == "cluster");
}

TEST_CASE("ancestor() with no match returns null node", "[config_node][ancestor]")
{
    const auto cfg = load_flat();
    const auto port_node = cfg.root()["cluster"]["server"]["port"];

    const auto anc = port_node.ancestor("nonexistent");
    CHECK_FALSE(anc.exists());
}

TEST_CASE("ancestor() matches base name of indexed segment", "[config_node][ancestor]")
{
    const auto cfg = load_indexed();
    // Path is "cluster/node[0]/port" -- ancestor("node") must match "cluster/node[0]".
    const auto port_node = cfg.root()["cluster"]["node"][std::size_t{0}]["port"];
    REQUIRE(port_node.exists());

    const auto anc = port_node.ancestor("node");
    REQUIRE(anc.exists());
    // base_name("node[0]") == "node", so the path must be "cluster/node[0]".
    CHECK(anc.path() == "cluster/node[0]");
}

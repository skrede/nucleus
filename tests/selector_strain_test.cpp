#include "nucleus/query/query.h"
#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

// in_strain() — ordinal-instance-correct; no cross-ordinal leak; pkey leaf
// included; container-level anchor returns empty; null ctx returns empty.
//
// Ordinal-indexed source paths survive the pkey fold (resolution_context skips paths
// where the segment after the container is an indexed token) so cluster/server[0]/name
// remains as-is in the resolved config while schema still declares primary_key_container.

using namespace nucleus;

namespace {

config_space build_server_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster",  anchor::root())));
    REQUIRE(builder.register_element(element("server",   anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(element("port",  anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(element("proto", anchor::keyspace("cluster/server"))));
    return std::move(builder).build();
}

// Ordinal paths survive fold — see resolution_context.h slice(): indexed segment
// directly after the pkey container is treated as a flat repeated leaf, not keyed.
config load_two_ordinal_strains(const config_space &space)
{
    runtime_source src;
    src.set("cluster/server[0]/name",  "web")
       .set("cluster/server[0]/port",  "443")
       .set("cluster/server[0]/proto", "https")
       .set("cluster/server[1]/name",  "api")
       .set("cluster/server[1]/port",  "8080")
       .set("cluster/server[1]/proto", "http");
    auto res = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(res.has_value());
    return std::move(*res);
}

bool path_in(const std::vector<config_node> &nodes, std::string_view p)
{
    return std::any_of(nodes.begin(), nodes.end(),
                       [p](const config_node &n) { return n.path() == p; });
}

}

// -------------------------------------------------------------------------
// Ordinal instance isolation: [0] fields do not appear in [1] query
// -------------------------------------------------------------------------

TEST_CASE("in_strain() from [0] anchor does not include [1] fields",
          "[selector]")
{
    const auto space = build_server_space();
    const auto ctx   = space.query_context();
    const auto cfg   = load_two_ordinal_strains(space);

    // Navigate to the first ordinal instance.
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    REQUIRE(anchor0.exists());
    CHECK(anchor0.path() == "cluster/server[0]");

    auto nodes = query(anchor0, ctx).in_strain().collect();

    // [0] fields must be present.
    CHECK(path_in(nodes, "cluster/server[0]/name"));
    CHECK(path_in(nodes, "cluster/server[0]/port"));
    CHECK(path_in(nodes, "cluster/server[0]/proto"));

    // [1] fields must NOT appear — no cross-ordinal leak.
    CHECK_FALSE(path_in(nodes, "cluster/server[1]/name"));
    CHECK_FALSE(path_in(nodes, "cluster/server[1]/port"));
    CHECK_FALSE(path_in(nodes, "cluster/server[1]/proto"));
}

TEST_CASE("in_strain() from [1] anchor does not include [0] fields",
          "[selector]")
{
    const auto space = build_server_space();
    const auto ctx   = space.query_context();
    const auto cfg   = load_two_ordinal_strains(space);

    const auto anchor1 = cfg.root()["cluster"]["server"][std::size_t{1}];
    REQUIRE(anchor1.exists());
    CHECK(anchor1.path() == "cluster/server[1]");

    auto nodes = query(anchor1, ctx).in_strain().collect();

    CHECK(path_in(nodes, "cluster/server[1]/name"));
    CHECK(path_in(nodes, "cluster/server[1]/port"));
    CHECK_FALSE(path_in(nodes, "cluster/server[0]/port"));
    CHECK_FALSE(path_in(nodes, "cluster/server[0]/name"));
}

// -------------------------------------------------------------------------
// in_strain() includes the pkey leaf
// -------------------------------------------------------------------------

TEST_CASE("in_strain() includes the retained pkey leaf",
          "[selector]")
{
    const auto space = build_server_space();
    const auto ctx   = space.query_context();
    const auto cfg   = load_two_ordinal_strains(space);

    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    REQUIRE(anchor0.exists());

    auto nodes = query(anchor0, ctx).in_strain().collect();

    // cluster/server[0]/name is the retained pkey leaf — must be included.
    CHECK(path_in(nodes, "cluster/server[0]/name"));
}

// -------------------------------------------------------------------------
// Container-level anchor yields empty (not error)
// -------------------------------------------------------------------------

TEST_CASE("in_strain() on container-level anchor yields empty",
          "[selector]")
{
    const auto space = build_server_space();
    const auto ctx   = space.query_context();
    const auto cfg   = load_two_ordinal_strains(space);

    // Anchor at the pkey container itself (not inside a specific instance).
    const auto container_anchor = cfg.root()["cluster"]["server"];
    REQUIRE(container_anchor.exists());
    CHECK(container_anchor.path() == "cluster/server");

    auto nodes = query(container_anchor, ctx).in_strain().collect();
    CHECK(nodes.empty());
}

// -------------------------------------------------------------------------
// in_strain() with null ctx yields empty (not error)
// -------------------------------------------------------------------------

TEST_CASE("in_strain() with null ctx yields empty", "[selector]")
{
    const auto space = build_server_space();
    const auto cfg   = load_two_ordinal_strains(space);

    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    REQUIRE(anchor0.exists());

    selector sel{anchor0, nullptr};
    CHECK(sel.in_strain().count() == 0);
}

// -------------------------------------------------------------------------
// Root-level anchor: in_strain() still correctly filters by [N] instance
// -------------------------------------------------------------------------

TEST_CASE("in_strain() from root with [0] ordinal anchor selects [0] strain only",
          "[selector]")
{
    const auto space = build_server_space();
    const auto ctx   = space.query_context();
    const auto cfg   = load_two_ordinal_strains(space);

    // Anchor is the [0] instance container; query from it collects all [0] descendants.
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    REQUIRE(anchor0.exists());

    // in_strain() from the [0] instance anchor excludes [1] fields even when the
    // visit traversal could otherwise reach [1] via the repeated parent.
    // (visit from anchor0 only traverses under cluster/server[0], so the real test
    // is that in_strain() from the ROOT with the [0] context still excludes [1].)
    auto nodes_from_root = query(cfg.root(), ctx).in_strain().collect();
    // Root anchor has empty path — pkey_cont="cluster/server", empty starts_with always,
    // but anchor_path[0] would need to be '['. Root path is "" so within_instance=false.
    CHECK(nodes_from_root.empty());

    // From the [0] container — all [0] nodes included, no [1] nodes.
    auto nodes = query(anchor0, ctx).in_strain().collect();
    CHECK(path_in(nodes, "cluster/server[0]/name"));
    CHECK(path_in(nodes, "cluster/server[0]/proto"));
    CHECK_FALSE(path_in(nodes, "cluster/server[1]/port"));
}

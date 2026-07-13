#include "nucleus/query/query.h"
#include "nucleus/config_space.h"
#include "nucleus/config.h"
#include "nucleus/identity.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

// owned_by() — ==-match only; never-registered token yields empty;
// anonymous token matches nothing (distinct pointer identity per construction);
// tagged tokens compare by payload.

using namespace nucleus;

namespace {

config load_two_owners(config_space &space,
                       const owner_token &owner_a,
                       const owner_token &owner_b)
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root()), owner_a));
    REQUIRE(builder.register_element(element("port",    anchor::keyspace("cluster")), owner_a));
    REQUIRE(builder.register_element(element("host",    anchor::keyspace("cluster")), owner_b));
    space = std::move(builder).build();

    runtime_source src;
    src.set("cluster/port", "8080");
    src.set("cluster/host", "localhost");
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
// Tagged token: == match by payload
// -------------------------------------------------------------------------

TEST_CASE("owned_by() with tagged token matches registered elements",
          "[selector]")
{
    owner_token token_a(std::string("plugin.a"));
    owner_token token_b(std::string("plugin.b"));

    config_space space;
    const auto cfg = load_two_owners(space, token_a, token_b);
    const auto ctx = space.query_context();

    auto nodes_a = query(cfg.root(), ctx).owned_by(owner_token(std::string("plugin.a"))).collect();

    // cluster and cluster/port were registered by plugin.a.
    CHECK(path_in(nodes_a, "cluster"));
    CHECK(path_in(nodes_a, "cluster/port"));

    // cluster/host was registered by plugin.b — must not appear.
    CHECK_FALSE(path_in(nodes_a, "cluster/host"));
}

TEST_CASE("owned_by() with a different tag yields its own elements",
          "[selector]")
{
    owner_token token_a(std::string("plugin.a"));
    owner_token token_b(std::string("plugin.b"));

    config_space space;
    const auto cfg = load_two_owners(space, token_a, token_b);
    const auto ctx = space.query_context();

    auto nodes_b = query(cfg.root(), ctx).owned_by(owner_token(std::string("plugin.b"))).collect();

    CHECK(path_in(nodes_b, "cluster/host"));
    CHECK_FALSE(path_in(nodes_b, "cluster/port"));
}

// -------------------------------------------------------------------------
// Never-registered token yields empty result (not error)
// -------------------------------------------------------------------------

TEST_CASE("owned_by() with a never-registered token yields empty",
          "[selector]")
{
    owner_token token_a(std::string("plugin.a"));
    owner_token token_b(std::string("plugin.b"));

    config_space space;
    const auto cfg = load_two_owners(space, token_a, token_b);
    const auto ctx = space.query_context();

    auto nodes = query(cfg.root(), ctx)
        .owned_by(owner_token(std::string("unregistered")))
        .collect();

    CHECK(nodes.empty());
}

// -------------------------------------------------------------------------
// Anonymous tokens never collide (each is a distinct identity)
// -------------------------------------------------------------------------

TEST_CASE("Anonymous default-constructed owner tokens never match each other",
          "[selector]")
{
    config_space_builder builder;
    owner_token anon_a; // used during registration
    REQUIRE(builder.register_element(element("cluster", anchor::root()), anon_a));
    REQUIRE(builder.register_element(element("port", anchor::keyspace("cluster"))));
    auto space = std::move(builder).build();
    const auto ctx = space.query_context();

    runtime_source src;
    src.set("cluster/port", "9090");
    auto res = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(res.has_value());

    // A freshly-constructed anonymous token is distinct from anon_a.
    owner_token anon_b; // different identity
    auto nodes = query(res->root(), ctx).owned_by(anon_b).collect();
    CHECK(nodes.empty());
}

TEST_CASE("owned_by(same_anon_token) matches the registered element",
          "[selector]")
{
    config_space_builder builder;
    owner_token anon;
    REQUIRE(builder.register_element(element("cluster", anchor::root()), anon));
    REQUIRE(builder.register_element(element("port", anchor::keyspace("cluster"))));
    auto space = std::move(builder).build();
    const auto ctx = space.query_context();

    runtime_source src;
    src.set("cluster/port", "9090");
    auto res = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(res.has_value());

    // The SAME anon token object must match via pointer identity.
    auto nodes = query(res->root(), ctx).owned_by(anon).collect();
    CHECK_FALSE(nodes.empty());
    bool has_cluster = std::any_of(nodes.begin(), nodes.end(),
                                   [](const config_node &n) { return n.path() == "cluster"; });
    CHECK(has_cluster);
}

// -------------------------------------------------------------------------
// owned_by() over a schema that registers no owners yields empty (not error)
// -------------------------------------------------------------------------

TEST_CASE("owned_by() with no registered owners yields empty", "[selector]")
{
    auto space = config_space_builder{}.build();

    runtime_source src;
    src.set("cluster/x", "1");
    auto res = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(res.has_value());

    selector sel{res->root(), space.query_context()};
    CHECK(sel.owned_by(owner_token(std::string("any"))).count() == 0);
}

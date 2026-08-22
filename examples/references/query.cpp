// query: demonstrates the programmatic query/selector API.
//
// Covers: space.query_context() entry, one() loud semantics,
// role() selectors, owned_by(), in_strain(), collect().
// Domain-neutral vocabulary (server/primary/secondary).

#include "nucleus/query/query.h"
#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/identity.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

using namespace nucleus;

// Schema: cluster/server (keyed by "name") with port and host leaves.
// Elements registered under a named owner so owned_by() can be shown.
static owner_token network_owner(std::string("network.module"));

static config_space make_server_space()
{
    config_space_builder builder;
    builder.register_element(element("cluster", anchor::root()), network_owner);
    builder.register_element(element("server", anchor::keyspace("cluster")), network_owner);
    builder.register_element(
            primary_key_element("name", anchor::keyspace("cluster/server")), network_owner);
    builder.register_element(
            element("port", anchor::keyspace("cluster/server")), network_owner);
    builder.register_element(
            element("host", anchor::keyspace("cluster/server")));
    return std::move(builder).build();
}

// Helper: print a header and a list of matched node paths.
static void print_nodes(const char *header, const std::vector<config_node> &nodes)
{
    std::cout << "\n--- " << header << " ---\n";
    if(nodes.empty())
    {
        std::cout << "  (none)\n";
        return;
    }
    for(const auto &n : nodes)
        std::cout << "  " << n.path() << '\n';
}

// Ordinal paths survive the pkey fold when the segment after the
// keyed container is already a bracket-indexed token.
static runtime_source make_server_source()
{
    runtime_source source;
    source.set("cluster/server[0]/name", "primary");
    source.set("cluster/server[0]/port", "8080");
    source.set("cluster/server[0]/host", "10.0.0.1");
    source.set("cluster/server[1]/name", "secondary");
    source.set("cluster/server[1]/port", "9090");
    source.set("cluster/server[1]/host", "10.0.0.2");
    return source;
}

static void show_primary_key_nodes(const config &cfg, const schema_query_context &ctx)
{
    const auto nodes = query(cfg.root(), ctx).role(node_role::primary_key).collect();
    print_nodes("role(primary_key) — all pkey leaves", nodes);
}

// one() loud semantics — two server instances mean two pkey nodes.
// one() returns errc::ambiguous_result when more than one node matches.
static void show_ambiguous_one(const config &cfg, const schema_query_context &ctx)
{
    std::cout << "\n--- one() on many matches (two servers -> ambiguous_result) ---\n";
    const auto ambiguous = query(cfg.root(), ctx).role(node_role::primary_key).one();
    if(!ambiguous)
        std::cout << "  error: " << ambiguous.error().message << '\n';
    else
        std::cout << "  node: " << ambiguous->path() << '\n';
}

// server[0]/name is the only pkey leaf under the first instance.
static void show_single_one(const config &cfg, const schema_query_context &ctx)
{
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    const auto single  = query(anchor0, ctx).role(node_role::primary_key).one();
    std::cout << "\n--- one() on server[0] (single pkey) ---\n";
    if(single)
        std::cout << "  name: " << single->value().value_or("(absent)") << '\n';
    else
        std::cout << "  error: " << single.error().message << '\n';
}

static void show_leaves(const config &cfg, const schema_query_context &ctx)
{
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    const auto nodes   = query(anchor0, ctx).children().leaves().collect();
    print_nodes("children().leaves() under server[0]", nodes);
}

static void show_owned_nodes(const config &cfg, const schema_query_context &ctx,
                             const owner_token &owner)
{
    const auto nodes = query(cfg.root(), ctx).owned_by(owner).collect();
    print_nodes("owned_by(network_owner)", nodes);
}

// in_strain(): all nodes belonging to server[0], with no cross-instance leak.
static void show_strain_nodes(const config &cfg, const schema_query_context &ctx)
{
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    const auto nodes   = query(anchor0, ctx).in_strain().collect();
    print_nodes("in_strain() from server[0]", nodes);
}

int main()
{
    const config_space space  = make_server_space();
    runtime_source     source = make_server_source();
    const auto         loaded = load_config(space, source_stack{std::move(source)}, {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const config &cfg = *loaded;
    // The ctx borrows space; it must not outlive the space.
    const schema_query_context ctx = space.query_context();
    show_primary_key_nodes(cfg, ctx);
    show_ambiguous_one(cfg, ctx);
    show_single_one(cfg, ctx);
    show_leaves(cfg, ctx);
    show_owned_nodes(cfg, ctx, network_owner);
    show_strain_nodes(cfg, ctx);
    return 0;
}

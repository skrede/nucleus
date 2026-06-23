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
    builder.register_element(element("cluster", anchor::root()),  network_owner);
    builder.register_element(element("server",  anchor::keyspace("cluster")), network_owner);
    builder.register_element(
        primary_key_element("name",  anchor::keyspace("cluster/server")), network_owner);
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

int main()
{
    auto space = make_server_space();

    // 1. Load two server instances via ordinal-indexed runtime source.
    //    Ordinal paths survive the pkey fold when the segment after the
    //    keyed container is already a bracket-indexed token.
    runtime_source src;
    src.set("cluster/server[0]/name", "primary");
    src.set("cluster/server[0]/port", "8080");
    src.set("cluster/server[0]/host", "10.0.0.1");
    src.set("cluster/server[1]/name", "secondary");
    src.set("cluster/server[1]/port", "9090");
    src.set("cluster/server[1]/host", "10.0.0.2");

    auto loaded = load_config(space, source_stack{std::move(src)}, {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const auto &cfg = *loaded;

    // 2. Build the transient schema_query_context.
    //    The ctx borrows `space`; it must not outlive the space.
    const auto ctx = space.query_context();

    // 3. role(primary_key): enumerate the pkey leaves across all instances.
    auto pkey_nodes = query(cfg.root(), ctx).role(node_role::primary_key).collect();
    print_nodes("role(primary_key) — all pkey leaves", pkey_nodes);

    // 4. one() loud semantics — two server instances mean two pkey nodes.
    //    one() returns errc::ambiguous_result when more than one node matches.
    std::cout << "\n--- one() on many matches (two servers -> ambiguous_result) ---\n";
    auto ambiguous = query(cfg.root(), ctx).role(node_role::primary_key).one();
    if(!ambiguous)
        std::cout << "  error: " << ambiguous.error().message << '\n';
    else
        std::cout << "  node: " << ambiguous->path() << '\n';

    // one() on exactly one match (server[0]/name is the only pkey leaf under [0]).
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    auto single = query(anchor0, ctx).role(node_role::primary_key).one();
    std::cout << "\n--- one() on server[0] (single pkey) ---\n";
    if(single)
        std::cout << "  name: " << single->value().value_or("(absent)") << '\n';
    else
        std::cout << "  error: " << single.error().message << '\n';

    // 5. leaves() under server[0]: all scalar leaves of the first instance.
    auto leaves = query(anchor0, ctx).children().leaves().collect();
    print_nodes("children().leaves() under server[0]", leaves);

    // 6. owned_by(): elements registered under network_owner vs. the rest.
    auto owned = query(cfg.root(), ctx).owned_by(network_owner).collect();
    print_nodes("owned_by(network_owner)", owned);

    // 7. in_strain(): all nodes belonging to server[0] (no cross-instance leak).
    auto strain = query(anchor0, ctx).in_strain().collect();
    print_nodes("in_strain() from server[0]", strain);

    return 0;
}

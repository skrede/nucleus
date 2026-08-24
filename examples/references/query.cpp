#include "nucleus/query/query.h"

#include "nucleus/config.h"
#include "nucleus/identity.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <string>
#include <vector>
#include <cstddef>
#include <ostream>
#include <utility>
#include <iostream>
#include <string_view>

static nucleus::owner_token network_owner(std::string("network.module"));

template<typename Builder>
static nucleus::registration_result define_server_space(Builder &builder)
{
    if(auto result = builder.register_element(nucleus::element("cluster", nucleus::anchor::root()), network_owner); !result)
        return result;
    if(auto result = builder.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster")), network_owner); !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server")), network_owner);
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::element("port", nucleus::anchor::keyspace("cluster/server")), network_owner);
       !result)
        return result;
    return builder.register_element(
            nucleus::element("host", nucleus::anchor::keyspace("cluster/server")), network_owner);
}

template<typename Builder>
static nucleus::expected<nucleus::config_space, nucleus::error> make_server_space(Builder &builder)
{
    if(auto result = define_server_space(builder); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

static nucleus::expected<nucleus::config_space, nucleus::error> make_server_space()
{
    nucleus::config_space_builder builder;
    return make_server_space(builder);
}

static void print_nodes(std::ostream &output, const char *header,
                        const std::vector<nucleus::config_node> &nodes)
{
    output << "\n--- " << header << " ---\n";
    if(nodes.empty())
    {
        output << "  (none)\n";
        return;
    }
    for(const auto &n : nodes)
        output << "  " << n.path() << '\n';
}

static nucleus::runtime_source make_server_source()
{
    nucleus::runtime_source source;
    source.set("cluster/server[0]/name", "primary");
    source.set("cluster/server[0]/port", "8080");
    source.set("cluster/server[0]/host", "10.0.0.1");
    source.set("cluster/server[1]/name", "secondary");
    source.set("cluster/server[1]/port", "9090");
    source.set("cluster/server[1]/host", "10.0.0.2");
    return source;
}

static void show_primary_key_nodes(std::ostream &output, const nucleus::config &cfg,
                                   const nucleus::schema_query_context &ctx)
{
    const auto nodes = nucleus::query(cfg.root(), ctx).role(nucleus::node_role::primary_key).collect();
    print_nodes(output, "role(primary_key) — all pkey leaves", nodes);
}

static int show_ambiguous_one(std::ostream &output, std::ostream &errors,
                              nucleus::expected<nucleus::config_node, nucleus::error> ambiguous)
{
    output << "\n--- one() on many matches (two servers -> ambiguous_result) ---\n";
    constexpr std::string_view message =
            "query matched 2 nodes; one() requires exactly one match";
    if(ambiguous || ambiguous.error().code != nucleus::errc::ambiguous_result ||
       ambiguous.error().message != message)
    {
        errors << "unexpected ambiguous query result\n";
        return 1;
    }
    output << "  error: " << ambiguous.error().message << '\n';
    return 0;
}

static int show_single_one(std::ostream &output, std::ostream &errors,
                           nucleus::expected<nucleus::config_node, nucleus::error> single)
{
    output << "\n--- one() on server[0] (single pkey) ---\n";
    if(!single)
    {
        errors << "unexpected single query error: " << single.error() << '\n';
        return 1;
    }
    const auto value = single->value();
    if(!value || *value != "primary")
    {
        errors << "unexpected single query value\n";
        return 1;
    }
    output << "  name: " << *value << '\n';
    return 0;
}

static void show_leaves(std::ostream &output, const nucleus::config &cfg,
                        const nucleus::schema_query_context &ctx)
{
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    const auto nodes   = nucleus::query(anchor0, ctx).children().leaves().collect();
    print_nodes(output, "children().leaves() under server[0]", nodes);
}

static void show_owned_nodes(std::ostream &output, const nucleus::config &cfg,
                             const nucleus::schema_query_context &ctx,
                             const nucleus::owner_token          &owner)
{
    const auto nodes = nucleus::query(cfg.root(), ctx).owned_by(owner).collect();
    print_nodes(output, "owned_by(network_owner)", nodes);
}

static void show_strain_nodes(std::ostream &output, const nucleus::config &cfg,
                              const nucleus::schema_query_context &ctx)
{
    const auto anchor0 = cfg.root()["cluster"]["server"][std::size_t{0}];
    const auto nodes   = nucleus::query(anchor0, ctx).in_strain().collect();
    print_nodes(output, "in_strain() from server[0]", nodes);
}

static int run_queries(const nucleus::config &cfg, const nucleus::schema_query_context &ctx,
                       nucleus::expected<nucleus::config_node, nucleus::error> ambiguous,
                       nucleus::expected<nucleus::config_node, nucleus::error> single,
                       std::ostream &output, std::ostream &errors)
{
    show_primary_key_nodes(output, cfg, ctx);
    if(const int status = show_ambiguous_one(output, errors, std::move(ambiguous)); status != 0)
        return status;
    if(const int status = show_single_one(output, errors, std::move(single)); status != 0)
        return status;
    show_leaves(output, cfg, ctx);
    show_owned_nodes(output, cfg, ctx, network_owner);
    show_strain_nodes(output, cfg, ctx);
    return 0;
}

static int run_queries(const nucleus::config_space &space, std::ostream &output, std::ostream &errors)
{
    nucleus::runtime_source source = make_server_source();
    const auto              loaded = nucleus::load_config(space, nucleus::source_stack{std::move(source)}, {});
    if(!loaded)
    {
        errors << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const nucleus::config              &cfg       = *loaded;
    const nucleus::schema_query_context ctx       = space.query_context();
    const auto                          ambiguous = nucleus::query(cfg.root(), ctx).role(nucleus::node_role::primary_key).one();
    const auto                          anchor0   = cfg.root()["cluster"]["server"][std::size_t{0}];
    const auto                          single    = nucleus::query(anchor0, ctx).role(nucleus::node_role::primary_key).one();
    return run_queries(cfg, ctx, ambiguous, single, output, errors);
}

static int run_query_example(nucleus::expected<nucleus::config_space, nucleus::error> space, std::ostream &output,
                             std::ostream &errors)
{
    if(!space)
    {
        errors << "space setup failed: " << space.error() << '\n';
        return 1;
    }
    return run_queries(*space, output, errors);
}

int main()
{
    return run_query_example(make_server_space(), std::cout, std::cerr);
}

#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/strain_scope.h"

#include "support/builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

namespace {

using policy = nucleus::strain_scope_policy;
using chain  = std::map<std::string, std::string>;

const std::string base_origin    = "[0|path:base.xml|anonymous|0]";
const std::string derived_origin = "[1|path:derived.xml|anonymous|1]";
const std::string stack_origin   = "[2|stack[0]|anonymous|-]";

constexpr policy every_policy[] = {policy::file_level, policy::space_open_container_closed,
                                   policy::container_open_until_next_strain};

nucleus::config_space pool_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    REQUIRE(builder.register_element(nucleus::repeated_element("pool", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::element("label", anchor::keyspace("cluster/pool"))));
    REQUIRE(builder.register_element(nucleus::repeated_element("member", anchor::keyspace("cluster/pool"))));
    REQUIRE(builder.register_element(nucleus::element("addr", anchor::keyspace("cluster/pool/member"))));
    REQUIRE(builder.register_element(nucleus::element("weight", anchor::keyspace("cluster/pool/member"))));
    return nucleus::builder_result_test::built(builder);
}

nucleus::config_space sink_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    REQUIRE(builder.register_element(nucleus::element("logging", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::repeated_element("sink", anchor::keyspace("cluster/logging"))));
    return nucleus::builder_result_test::built(builder);
}

chain pool_chain(const std::string &derived_pool, const std::string &base_member = "<addr>a0</addr>")
{
    const std::string server = "<server name=\"primary\"><route><port>80</port><method>get</method></route></server>";
    return {{"base.xml", "<cluster><pool><label>L0</label><member>" + base_member + "</member></pool>" + server + "</cluster>"},
            {"derived.xml", "<cluster inherit=\"base.xml\"><pool>" + derived_pool + "</pool><server name=\"primary\" extend=\"narrow\"></server></cluster>"}};
}

nucleus::source_stack runtime_stack(const std::string &key, const std::string &value)
{
    return nucleus::source_stack{nucleus::shapes::runtime_layer({{key, value}})};
}

}

TEST_CASE("a flat source replacing a general instance keeps a nested value an excluded layer displaced out",
          "[collection_shapes][restoration][nested]")
{
    const chain documents = pool_chain("<member><addr>d0</addr></member>");
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(pool_space(), documents, "derived.xml", scope, runtime_stack("cluster/pool[0]/label", "S")),
                                       {"cluster/pool[0]/label = S " + stack_origin}, {"cluster/pool[0]/member[0]/addr"});
    }
}

TEST_CASE("a flat source replacing an inner general instance keeps the base's fields out after an excluded layer emptied the outer one",
          "[collection_shapes][restoration][nested]")
{
    const chain documents = pool_chain("<label>D</label>", "<addr>a0</addr><weight>w0</weight>");
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        const std::string label = scope == policy::file_level ? "cluster/pool[0]/label = L0 " + base_origin
                                                              : "cluster/pool[0]/label = D " + derived_origin;
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(pool_space(), documents, "derived.xml", scope,
                                                                   runtime_stack("cluster/pool[0]/member[0]/addr", "S")),
                                       {label, "cluster/pool[0]/member[0]/addr = S " + stack_origin}, {"cluster/pool[0]/member[0]/weight"});
    }
}

TEST_CASE("a general repeated container nested beside a keyed container loads",
          "[collection_shapes][keyed][bucketing][nested]")
{
    const chain documents = pool_chain("");
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(pool_space(), documents, "base.xml", scope),
                                       {"cluster/pool[0]/label = L0 " + base_origin, "cluster/pool[0]/member[0]/addr = a0 " + base_origin,
                                        "cluster/server/route[0]/port = 80 " + base_origin, "cluster/server/name = primary " + base_origin},
                                       {});
    }
}

TEST_CASE("a repeated leaf in a sibling container loads beside a keyed container",
          "[collection_shapes][keyed][bucketing]")
{
    const chain documents = {{"base.xml", "<cluster><logging><sink>s0</sink><sink>s1</sink></logging><server name=\"primary\">"
                                          "<route><port>80</port><method>get</method></route></server></cluster>"}};
    for(const policy scope : every_policy)
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(sink_space(), documents, "base.xml", scope),
                                       {"cluster/logging/sink[0] = s0 " + base_origin, "cluster/logging/sink[1] = s1 " + base_origin,
                                        "cluster/server/route[0]/port = 80 " + base_origin, "cluster/server/name = primary " + base_origin},
                                       {});
    }
}

TEST_CASE("a file_level re-open over a nested general container keeps the base's instance",
          "[collection_shapes][keyed][bucketing][nested]")
{
    const chain documents = pool_chain("<label>D</label>");
    nucleus::shapes::require_lines(nucleus::shapes::load_chain(pool_space(), documents, "derived.xml", policy::file_level),
                                   {"cluster/pool[0]/label = L0 " + base_origin, "cluster/pool[0]/member[0]/addr = a0 " + base_origin,
                                    "cluster/server/name = primary " + derived_origin},
                                   {});
    for(const policy scope : {policy::space_open_container_closed, policy::container_open_until_next_strain})
    {
        CAPTURE(scope);
        nucleus::shapes::require_lines(nucleus::shapes::load_chain(pool_space(), documents, "derived.xml", scope),
                                       {"cluster/pool[0]/label = D " + derived_origin}, {"cluster/pool[0]/member[0]/addr"});
    }
}

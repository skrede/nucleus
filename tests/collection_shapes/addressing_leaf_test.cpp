#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "support/builder_result_test_support.h"

#include "nucleus/config_source/source_stack.h"
#include "nucleus/config_source/source_handle.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

nucleus::config_space cluster_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_cluster_nodes_routes(builder);
    return nucleus::builder_result_test::built(builder);
}

// Two node instances, each carrying a nested repeated container, so a key naming no
// instance has real siblings it could be confused with.
nucleus::load_result over_two_nodes(const nucleus::config_space &space,
                                    const std::string &key)
{
    return nucleus::load_config(space,
        nucleus::source_stack{
            xml_of("<cluster>"
                   "<node><port>10</port><tags><name>alpha</name></tags></node>"
                   "<node><port>20</port><tags><name>beta</name></tags></node>"
                   "</cluster>"),
            nucleus::shapes::runtime_layer({{key, "zzz"}})},
        {});
}

}

TEST_CASE("a repeated leaf arriving plain under a named instance still mints its ordinal",
          "[collection_shapes][addressing][leaf]")
{
    const nucleus::config_space space = cluster_space();

    const nucleus::load_result loaded = over_two_nodes(space, "cluster/node[0]/mark");
    REQUIRE(loaded);
    INFO(nucleus::shapes::serialize(loaded.value()));
    REQUIRE(loaded.value().get("cluster/node[0]/mark[0]") == "zzz");
    REQUIRE(loaded.value().get("cluster/node[1]/port") == "20");
}

TEST_CASE("a named instance's scalar and a top-level repeated leaf are untouched by the "
          "addressing rule",
          "[collection_shapes][addressing][leaf]")
{
    const nucleus::config_space space = cluster_space();

    const nucleus::load_result indexed = over_two_nodes(space, "cluster/node[0]/port");
    REQUIRE(indexed);
    REQUIRE(indexed.value().get("cluster/node[0]/port") == "zzz");
    REQUIRE(indexed.value().get("cluster/node[1]/port") == "20");

    const nucleus::load_result top_leaf = nucleus::load_config(space,
        nucleus::source_stack{xml_of("<cluster><zone>x</zone></cluster>"),
                              nucleus::shapes::runtime_layer({{"cluster/zone", "zzz"}})},
        {});
    REQUIRE(top_leaf);
    REQUIRE(top_leaf.value().get_all("cluster/zone") == std::vector<std::string>{"zzz"});
}

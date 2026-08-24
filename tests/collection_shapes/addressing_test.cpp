#include "collection_shapes.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "../builder_result_test_support.h"

#include "nucleus/schema/converters.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstdint>

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
    return builder.build();
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

TEST_CASE("a key whose repeated-container segments are unindexed fails the load, naming "
          "the source and the offending path",
          "[collection_shapes][addressing]")
{
    const nucleus::config_space space = cluster_space();

    for(const std::string key : {"cluster/node/tags", "cluster/node/tags[0]",
                                 "cluster/node/tags[0]/name", "cluster/node/port",
                                 "cluster/node[0]/tags"})
    {
        INFO("key: " << key);
        const nucleus::load_result loaded = over_two_nodes(space, key);
        CHECK_FALSE(loaded);
        if(!loaded)
        {
            CHECK(loaded.error().code == nucleus::errc::malformed_source);
            CHECK(loaded.error().message.find("stack[1]") != std::string::npos);
            CHECK(loaded.error().message.find(key) != std::string::npos);
        }
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

TEST_CASE("an ordinal segment outside the accepted domain fails, naming the source, "
          "the path and the bound",
          "[collection_shapes][diagnostics]")
{
    const nucleus::config_space space = cluster_space();
    const std::string key = "cluster/node/0000000000000000000/port";

    const nucleus::load_result loaded = over_two_nodes(space, key);
    REQUIRE_FALSE(loaded);
    INFO("message: " << loaded.error().message);
    REQUIRE(loaded.error().code == nucleus::errc::malformed_source);
    REQUIRE(loaded.error().message.find("stack[1]") != std::string::npos);
    REQUIRE(loaded.error().message.find(key) != std::string::npos);
    REQUIRE(loaded.error().message.find("4294967295") != std::string::npos);
}

TEST_CASE("a segment of non-ASCII digit characters is not read as an ordinal",
          "[collection_shapes][diagnostics]")
{
    const nucleus::config_space space = cluster_space();

    // U+0660 U+0661 written as bytes, so the file's own encoding cannot change what
    // the byte-wise recognizer sees.
    const nucleus::load_result loaded =
        over_two_nodes(space, "cluster/node/\xd9\xa0\xd9\xa1/port");
    const std::string message = loaded ? std::string() : loaded.error().message;
    INFO("message: " << message);
    REQUIRE(message.find("ordinal") == std::string::npos);
}

TEST_CASE("an empty source stack reaches resolution with no entries and produces no "
          "resolution diagnostic",
          "[collection_shapes][diagnostics]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("solo", nucleus::anchor::root())));
    const nucleus::config_space flat = nucleus::builder_result_test::built(builder);

    const nucleus::load_result loaded =
        nucleus::load_config(flat, nucleus::source_stack{}, {});
    INFO("message: " << (loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded);
    REQUIRE(loaded.value().keys().empty());

    // A nested schema hard-requires the nesting capability, which an empty stack has
    // no layer to provide; that shortfall is the capability gate's, not resolution's.
    const nucleus::load_result nested =
        nucleus::load_config(cluster_space(), nucleus::source_stack{}, {});
    REQUIRE_FALSE(nested);
    REQUIRE(nested.error().code == nucleus::errc::unmet_capability);
}

TEST_CASE("mixed canonical storage is rejected before schema-specific conversion",
          "[collection_shapes][addressing]")
{
    // A segment the schema does not declare repeated bypasses the declaration-aware
    // addressing rule, so the schema-independent storage gate owns this conflict.
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::element("x", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::typed_element<std::int32_t>(
                "port", nucleus::anchor::keyspace("cluster/x"))));
    const nucleus::config_space space = nucleus::builder_result_test::built(builder);

    const nucleus::load_result loaded = nucleus::load_config(space,
        nucleus::source_stack{nucleus::shapes::runtime_layer(
            {{"cluster/x/port", "1"}, {"cluster/x[0]/port", "2"}})},
        {});
    REQUIRE_FALSE(loaded);
    INFO("message: " << loaded.error().message);
    REQUIRE(loaded.error().code == nucleus::errc::schema_violation);
    REQUIRE(loaded.error().message.find("canonical path 'cluster/x/port'")
            != std::string::npos);
    REQUIRE(loaded.error().message.find(
                "concrete paths 'cluster/x/port' and 'cluster/x[0]/port'")
            != std::string::npos);
}

TEST_CASE("a malformed ordinal under a single-segment repeated container prefix still "
          "names the source and the path",
          "[collection_shapes][diagnostics]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(
        nucleus::repeated_element("node", nucleus::anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::element("port", nucleus::anchor::keyspace("node"))));
    const nucleus::config_space space = nucleus::builder_result_test::built(builder);
    const std::string key = "node/0000000000000000000/port";

    const nucleus::load_result loaded = nucleus::load_config(space,
        nucleus::source_stack{nucleus::shapes::runtime_layer({{key, "zzz"}})}, {});
    REQUIRE_FALSE(loaded);
    INFO("message: " << loaded.error().message);
    REQUIRE(loaded.error().message.find("stack[0]") != std::string::npos);
    REQUIRE(loaded.error().message.find(key) != std::string::npos);
}

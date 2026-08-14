#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

nucleus::config_space nested_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::repeated_element("node", nucleus::anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::repeated_element("route", nucleus::anchor::keyspace("cluster/server/node"))));
    REQUIRE(builder.register_element(nucleus::element("port", nucleus::anchor::keyspace("cluster/server/node/route"))));
    REQUIRE(builder.register_element(nucleus::element("method", nucleus::anchor::keyspace("cluster/server/node/route"))));
    return builder.build();
}

std::string nested_base()
{
    return "<cluster><server name=\"primary\">"
           "<node><route><port>100</port><method>a0</method></route>"
           "<route><port>101</port><method>a1</method></route>"
           "<route><port>102</port><method>a2</method></route></node>"
           "<node><route><port>200</port><method>b0</method></route>"
           "<route><port>201</port><method>b1</method></route>"
           "<route><port>202</port><method>b2</method></route></node>"
           "</server></cluster>";
}

std::string nested_derived()
{
    return "<cluster inherit=\"base.xml\"><server name=\"primary\" extend=\"narrow\">"
           "<node><route><port>900</port><method>x</method></route></node>"
           "</server></cluster>";
}

nucleus::load_result load_chain(const nucleus::config_space &space)
{
    const std::string     base    = nested_base();
    const std::string     derived = nested_derived();
    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document  = [base, derived](const std::string &path)
    {
        return xml_of(nucleus::shapes::filename_of(path) == "base.xml" ? base : derived);
    };
    opts.selection = "primary";
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

nucleus::config_space keyed_merge_space()
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("cluster", nucleus::anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(nucleus::merging(
            nucleus::repeated_element("output", nucleus::anchor::keyspace("cluster/server")),
            nucleus::merge_mode::unite)));
    REQUIRE(builder.register_element(nucleus::element("id", nucleus::anchor::keyspace("cluster/server/output"))));
    REQUIRE(builder.register_element(nucleus::element("addr", nucleus::anchor::keyspace("cluster/server/output"))));
    REQUIRE(builder.register_identity_group(
            nucleus::identity_group("output_ids", nucleus::anchor::keyspace("cluster/server"))
                    .members({"output"})
                    .field("id")));
    return builder.build();
}

std::string keyed_merge_document()
{
    return "<cluster><server name=\"primary\">"
           "<output><id>a</id><addr>one</addr></output>"
           "<output><id>b</id><addr>two</addr></output>"
           "<output><id>c</id><addr>three</addr></output>"
           "</server></cluster>";
}

nucleus::load_result load_keyed_merge(const nucleus::config_space &space)
{
    nucleus::load_options opts;
    opts.selection = "primary";
    return nucleus::load_config(
            space,
            nucleus::source_stack{
                    xml_of(keyed_merge_document()),
                    nucleus::shapes::runtime_layer(
                            {{"cluster/server/primary/output[0]/id", "d"},
                             {"cluster/server/primary/output[0]/addr", "four"}})},
            opts);
}

}

TEST_CASE("nested compaction is confined to the affected outer instance",
          "[collection_shapes][keyed][compaction][nested]")
{
    const nucleus::config_space space  = nested_space();
    const nucleus::load_result  loaded = load_chain(space);
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));
    REQUIRE(cfg.get("cluster/server/node[0]/route[0]/port") == "101");
    REQUIRE(cfg.get("cluster/server/node[0]/route[0]/method") == "a1");
    REQUIRE(cfg.get("cluster/server/node[0]/route[1]/port") == "102");
    REQUIRE(cfg.get("cluster/server/node[0]/route[1]/method") == "a2");
    REQUIRE_FALSE(cfg.contains("cluster/server/node[0]/route[2]/port"));
    REQUIRE(cfg.get("cluster/server/node[1]/route[0]/port") == "200");
    REQUIRE(cfg.get("cluster/server/node[1]/route[1]/port") == "201");
    REQUIRE(cfg.get("cluster/server/node[1]/route[2]/port") == "202");
}

TEST_CASE("keyed merge ordinals remain in the order assigned by the merge",
          "[collection_shapes][keyed][compaction][merge]")
{
    const nucleus::config_space space  = keyed_merge_space();
    const nucleus::load_result  loaded = load_keyed_merge(space);
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));
    REQUIRE(cfg.get("cluster/server/output[0]/id") == "a");
    REQUIRE(cfg.get("cluster/server/output[1]/id") == "b");
    REQUIRE(cfg.get("cluster/server/output[2]/id") == "c");
    REQUIRE(cfg.get("cluster/server/output[3]/id") == "d");
    REQUIRE(cfg.get("cluster/server/output[3]/addr") == "four");
}

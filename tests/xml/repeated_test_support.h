#ifndef HPP_GUARD_NUCLEUS_TESTS_XML_REPEATED_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_XML_REPEATED_TEST_SUPPORT_H

#include "nucleus/config.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <utility>

namespace nucleus::xml_repeated_test {

inline config checked_config(std::map<std::string, std::string> values)
{
    auto made = config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

inline xml_source xml_of(std::string text)
{
    return xml_source::from(xml_source_options::of_string(std::move(text)));
}

inline schema_registry cluster_nodes_registry()
{
    schema_registry registry;
    REQUIRE(registry.attach(element("cluster", anchor::root())));
    REQUIRE(registry.attach(
            repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(registry.attach(
            element("port", anchor::keyspace("cluster/node"))));
    return registry;
}

inline schema_registry cluster_nodes_routes_registry()
{
    schema_registry registry;
    REQUIRE(registry.attach(element("cluster", anchor::root())));
    REQUIRE(registry.attach(
            repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(registry.attach(
            repeated_element("route", anchor::keyspace("cluster/node"))));
    REQUIRE(registry.attach(
            element("method", anchor::keyspace("cluster/node/route"))));
    return registry;
}

inline schema_registry cluster_plain_server_registry()
{
    schema_registry registry;
    REQUIRE(registry.attach(element("cluster", anchor::root())));
    REQUIRE(registry.attach(element("server", anchor::keyspace("cluster"))));
    REQUIRE(registry.attach(
            element("port", anchor::keyspace("cluster/server"))));
    return registry;
}

inline config_space cluster_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            element("port", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            element("metrics", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            element("latency", anchor::keyspace("cluster/node/metrics"))));
    return nucleus::builder_result_test::built(builder);
}

inline config_space simple_cluster_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            element("port", anchor::keyspace("cluster/node"))));
    return nucleus::builder_result_test::built(builder);
}

inline config_space repeated_tags_space()
{
    config_space_builder builder;
    REQUIRE(builder.register_element(element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            repeated_element("tags", anchor::keyspace("cluster"))));
    return nucleus::builder_result_test::built(builder);
}

inline source_handle source_of(std::string text)
{
    return source_handle(xml_of(std::move(text)));
}

inline load_options document_options(std::string document)
{
    load_options options;
    options.document_paths = {"doc.xml"};
    options.make_document  = [text = std::move(document)](const std::string &)
    {
        return source_of(text);
    };
    return options;
}

}

#endif

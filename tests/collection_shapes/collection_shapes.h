#ifndef HPP_GUARD_NUCLEUS_TESTS_COLLECTION_SHAPES_COLLECTION_SHAPES_H
#define HPP_GUARD_NUCLEUS_TESTS_COLLECTION_SHAPES_COLLECTION_SHAPES_H

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <functional>

// Shared shapes for the collection suites: schema builders whose repetition is
// nested and sibling-bearing, fixture-document factories, and a serializer that
// renders the whole recorded origin so a sibling instance keeping its value but
// losing its provenance still shows up as a difference.
namespace nucleus::shapes {

// cluster / node[] / { port, label, route[]/{port, method}, tags[]/name, mark[] }
// plus a top-level zone[]. node, route and tags are repeated containers; mark is a
// repeated leaf nested under a repeated container and zone one that is not.
inline void declare_cluster_nodes_routes(nucleus::config_space_builder &builder)
{
    using nucleus::anchor;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::element("port", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::element("label", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("route", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::element("port", anchor::keyspace("cluster/node/route"))));
    REQUIRE(builder.register_element(
        nucleus::element("method", anchor::keyspace("cluster/node/route"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("tags", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::element("name", anchor::keyspace("cluster/node/tags"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("mark", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("zone", anchor::keyspace("cluster"))));
}

// cluster / server (primary-keyed by name) / route[] / { port, method }. The
// container carries a primary key but declares no keyed merge mode, so its entries
// flow through the ordinary sweep with the transient key segment still in the path.
inline void declare_keyed_server_routes(nucleus::config_space_builder &builder)
{
    using nucleus::anchor;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
        nucleus::repeated_element("route", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
        nucleus::element("port", anchor::keyspace("cluster/server/route"))));
    REQUIRE(builder.register_element(
        nucleus::element("method", anchor::keyspace("cluster/server/route"))));
}

// node[] / {port, label}, server (primary-keyed by name) / port, and a plain motd
// leaf -- all anchored at the root, so under a named space they are direct children
// of the transparent root rather than of an enclosing element.
inline void declare_transparent_root_nodes(nucleus::config_space_builder &builder)
{
    using nucleus::anchor;
    REQUIRE(builder.register_element(nucleus::repeated_element("node", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("port", anchor::keyspace("node"))));
    REQUIRE(builder.register_element(nucleus::element("label", anchor::keyspace("node"))));
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(
        nucleus::primary_key_element("name", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::element("port", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::element("motd", anchor::root())));
}

// The filename portion of a (possibly absolute) path, so the factory can dispatch
// against an in-repo fixture directory without depending on the working directory.
inline std::string filename_of(const std::string &path)
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

inline nucleus::source_handle document(const std::string &case_dir, const std::string &name)
{
    return nucleus::source_handle(nucleus::xml_source::from(
        nucleus::xml_source_options::of_file(case_dir + "/" + filename_of(name))));
}

// Dispatches every requested document path to a file in one case directory, so
// inherit= and multi-path stacks both resolve against the same fixtures.
inline std::function<nucleus::source_handle(const std::string &)>
file_factory(std::string case_dir)
{
    return [dir = std::move(case_dir)](const std::string &path) -> nucleus::source_handle {
        return document(dir, path);
    };
}

inline nucleus::source_handle
runtime_layer(std::vector<std::pair<std::string, std::string>> entries)
{
    return nucleus::source_handle(nucleus::runtime_source(std::move(entries)));
}

// One line per key as `key = value [rank|label|owner|inheritance layer]`. The
// origin is rendered whole because an untouched sibling must keep its recorded
// provenance, not merely its value.
inline std::string serialize(const nucleus::config &config)
{
    std::string out;
    for(const std::string &key : config.keys())
    {
        const nucleus::origin *orig = config.provenance_of(key);
        const auto val = config.get(key);
        out += key + " = " + val.value_or(std::string()) + " [";
        if(orig == nullptr)
            out += "no origin";
        else
            out += std::to_string(orig->rank) + "|" + orig->layer + "|"
                 + (orig->owner.has_value() ? "tagged" : "anonymous") + "|"
                 + (orig->inheritance_layer.has_value()
                        ? std::to_string(orig->inheritance_layer.value())
                        : std::string("-"));
        out += "]\n";
    }
    return out;
}

}

#endif

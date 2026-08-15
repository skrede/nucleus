#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_emitter.h"
#include "nucleus/xml/xml_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <sstream>
#include <utility>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
    #error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

// The nested ordinal shapes of the shared harness plus a primary-keyed
// cluster/server carrying its own ordinal routes, so one space declares keyed and
// ordinal repetition together.
nucleus::config_space combined_space()
{
    using nucleus::anchor;
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_cluster_nodes_routes(builder);
    REQUIRE(builder.register_element(
            nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::primary_key_element("name", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("route", anchor::keyspace("cluster/server"))));
    REQUIRE(builder.register_element(
            nucleus::element("port", anchor::keyspace("cluster/server/route"))));
    return builder.build();
}

std::string case_dir()
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / "combined_route_shapes"));
    return (root / "combined_route_shapes").string();
}

nucleus::source_stack base_and_overlay()
{
    const std::string dir = case_dir();
    return nucleus::source_stack{nucleus::shapes::document(dir, "base.xml"),
                                 nucleus::shapes::document(dir, "overlay.xml")};
}

nucleus::load_result load_strain(const nucleus::config_space &space,
                                 nucleus::source_stack        stack)
{
    nucleus::load_options opts;
    opts.selection = "alpha";
    return nucleus::load_config(space, std::move(stack), opts);
}

// The serialized lines of one instance -- value and whole recorded origin -- so a
// sibling that kept its value while losing its provenance still reads as a change.
std::string lines_under(const std::string &serialized, const std::string &prefix)
{
    std::string        out;
    std::istringstream in(serialized);
    for(std::string line; std::getline(in, line);)
    {
        if(line.starts_with(prefix))
            out += line + "\n";
    }
    return out;
}

}

TEST_CASE("an overlay addressing one node's routes leaves the sibling node, the "
          "touched node's other levels and the keyed strain intact",
          "[collection_shapes][combined]")
{
    const nucleus::config_space space = combined_space();

    const nucleus::load_result loaded = load_strain(space, base_and_overlay());
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));

    REQUIRE(cfg.get("cluster/node[1]/port") == "20");
    REQUIRE(cfg.get("cluster/node[1]/route[0]/port") == "8080");
    REQUIRE(cfg.get("cluster/node[1]/route[1]/port") == "9090");
    REQUIRE(cfg.get("cluster/node[1]/route[1]/method") == "head");

    REQUIRE(cfg.get("cluster/node[0]/route[0]/port") == "81");
    REQUIRE(cfg.get("cluster/node[0]/route[1]/port") == "443");
    REQUIRE(cfg.get("cluster/node[0]/route[1]/method") == "post");
    REQUIRE(cfg.get("cluster/node[0]/port") == "10");

    REQUIRE(cfg.get("cluster/server/route[0]/port") == "5000");
    REQUIRE(cfg.get("cluster/server/route[1]/port") == "6000");
}

TEST_CASE("a runtime layer naming one instance over the same document set leaves "
          "every sibling instance intact",
          "[collection_shapes][combined]")
{
    const nucleus::config_space space = combined_space();
    const std::string           dir   = case_dir();

    const nucleus::load_result loaded = load_strain(space,
                                                    nucleus::source_stack{nucleus::shapes::document(dir, "base.xml"),
                                                                          nucleus::shapes::document(dir, "overlay.xml"),
                                                                          nucleus::shapes::runtime_layer(
                                                                                  {{"cluster/node[1]/route[0]/port", "7000"}})});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));

    REQUIRE(cfg.get("cluster/node[1]/route[0]/port") == "7000");
    REQUIRE(cfg.get("cluster/node[1]/route[1]/port") == "9090");
    REQUIRE(cfg.get("cluster/node[1]/port") == "20");
    REQUIRE(cfg.get("cluster/node[0]/route[0]/port") == "81");
    REQUIRE(cfg.get("cluster/node[0]/route[1]/port") == "443");
    REQUIRE(cfg.get("cluster/server/route[1]/port") == "6000");
}

TEST_CASE("an untouched sibling instance keeps its recorded origin, not merely its "
          "value, when a layer addresses another instance",
          "[collection_shapes][combined][provenance]")
{
    const nucleus::config_space space = combined_space();
    const std::string           dir   = case_dir();

    const nucleus::load_result base_only =
            load_strain(space, nucleus::source_stack{nucleus::shapes::document(dir, "base.xml")});
    const nucleus::load_result layered = load_strain(space, base_and_overlay());
    REQUIRE(base_only);
    REQUIRE(layered);

    const std::string alone =
            lines_under(nucleus::shapes::serialize(base_only.value()), "cluster/node[1]/");
    const std::string over =
            lines_under(nucleus::shapes::serialize(layered.value()), "cluster/node[1]/");
    INFO(alone);
    INFO(over);

    REQUIRE_FALSE(alone.empty());
    REQUIRE(over == alone);
}

TEST_CASE("the document set survives a load, emit and load again with the same "
          "instances",
          "[collection_shapes][combined][round_trip]")
{
    const nucleus::config_space space = combined_space();

    const nucleus::load_result first = load_strain(space, base_and_overlay());
    REQUIRE(first);

    std::ostringstream emitted;
    REQUIRE(nucleus::xml::emit_document(first.value(), space, emitted));
    const std::string text = emitted.str();
    INFO(text);

    const nucleus::load_result second = load_strain(space,
                                                    nucleus::source_stack{nucleus::source_handle(nucleus::xml_source::from(
                                                            nucleus::xml_source_options::of_string(text)))});
    REQUIRE(second);

    REQUIRE(second.value().keys() == first.value().keys());
    for(const std::string &key : first.value().keys())
        REQUIRE(second.value().get(key) == first.value().get(key));
}

#include "collection_shapes.h"

#include "support/builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
#error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
        nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

std::string fixture_dir(const std::string &name)
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / name));
    return (root / name).string();
}

nucleus::config_space cluster_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_cluster_nodes_routes(builder);
    return nucleus::builder_result_test::built(builder);
}

}

TEST_CASE("a layer supplying one node's nested collection leaves the sibling node's "
          "collection and scalars intact",
          "[collection_shapes][replace_by_ordinal][nested]")
{
    const nucleus::config_space space = cluster_space();
    const std::string dir = fixture_dir("nested_sibling_instances");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{nucleus::shapes::document(dir, "base.xml"),
                              nucleus::shapes::document(dir, "overlay.xml")},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));

    REQUIRE(cfg.get("cluster/node[0]/tags[0]/name") == "gamma");
    REQUIRE(cfg.get("cluster/node[0]/port") == "10");
    REQUIRE(cfg.get("cluster/node[1]/tags[0]/name") == "beta");
    REQUIRE(cfg.get("cluster/node[1]/port") == "20");
}

TEST_CASE("a runtime layer setting one instance's scalar leaves every sibling key "
          "with its value and its recorded origin",
          "[collection_shapes][replace_by_ordinal][provenance]")
{
    const nucleus::config_space space = cluster_space();

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{
            xml_of("<cluster>"
                   "<node><port>10</port><tags><name>alpha</name></tags></node>"
                   "<node><port>20</port><tags><name>beta</name></tags></node>"
                   "</cluster>"),
            nucleus::shapes::runtime_layer({{"cluster/node[0]/port", "99"}})},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));

    REQUIRE(cfg.get("cluster/node[0]/port") == "99");
    REQUIRE(cfg.get("cluster/node[1]/port") == "20");
    REQUIRE(cfg.get("cluster/node[1]/tags[0]/name") == "beta");

    const nucleus::origin *overridden = cfg.provenance_of("cluster/node[0]/port");
    const nucleus::origin *sibling = cfg.provenance_of("cluster/node[1]/port");
    const nucleus::origin *untouched = cfg.provenance_of("cluster/node[1]/tags[0]/name");
    REQUIRE(overridden != nullptr);
    REQUIRE(sibling != nullptr);
    REQUIRE(untouched != nullptr);
    REQUIRE(overridden->layer == "stack[1]");
    REQUIRE(sibling->layer == "stack[0]");
    REQUIRE(sibling->rank == untouched->rank);
    REQUIRE(sibling->owner == untouched->owner);
    REQUIRE(sibling->inheritance_layer == untouched->inheritance_layer);
}

TEST_CASE("a two-instance layer over a three-instance base resolves to three "
          "instances, the surplus surviving from the base",
          "[collection_shapes][replace_by_ordinal]")
{
    const nucleus::config_space space = cluster_space();

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{
            xml_of("<cluster>"
                   "<node><port>10</port></node>"
                   "<node><port>20</port></node>"
                   "<node><port>30</port></node>"
                   "</cluster>"),
            xml_of("<cluster>"
                   "<node><port>80</port><label>first</label></node>"
                   "<node><port>90</port></node>"
                   "</cluster>")},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));

    REQUIRE(cfg.get("cluster/node[0]/port") == "80");
    REQUIRE(cfg.get("cluster/node[0]/label") == "first");
    REQUIRE(cfg.get("cluster/node[1]/port") == "90");
    REQUIRE(cfg.get("cluster/node[2]/port") == "30");
}

TEST_CASE("two sibling repeated leaves inside one container instance resolve to "
          "two distinct indexed keys",
          "[collection_shapes][leaf][nested]")
{
    const nucleus::config_space space = cluster_space();

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{
            xml_of("<cluster><node><mark>a</mark><mark>b</mark></node></cluster>")},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));

    REQUIRE(cfg.get("cluster/node[0]/mark[0]") == "a");
    REQUIRE(cfg.get("cluster/node[0]/mark[1]") == "b");
}

TEST_CASE("a repeated leaf outside any repeated container is still replaced as a "
          "whole value list by a higher layer",
          "[collection_shapes][leaf][replace_by_ordinal]")
{
    const nucleus::config_space space = cluster_space();

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{
            xml_of("<cluster><zone>x</zone><zone>y</zone><zone>z</zone></cluster>"),
            xml_of("<cluster><zone>p</zone></cluster>")},
        {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();
    INFO(nucleus::shapes::serialize(cfg));

    REQUIRE(cfg.get_all("cluster/zone") == std::vector<std::string>{"p"});
}

TEST_CASE("loading the same source stack twice yields byte-identical output "
          "including provenance",
          "[collection_shapes][provenance]")
{
    const nucleus::config_space space = cluster_space();
    const std::string dir = fixture_dir("nested_sibling_instances");

    const auto load_once = [&] {
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{nucleus::shapes::document(dir, "base.xml"),
                                  nucleus::shapes::document(dir, "overlay.xml")},
            {});
        REQUIRE(loaded);
        return nucleus::shapes::serialize(loaded.value());
    };

    const std::string first = load_once();
    const std::string second = load_once();
    INFO(first);
    REQUIRE(first == second);
}

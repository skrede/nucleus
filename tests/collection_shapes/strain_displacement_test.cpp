#include "collection_shapes.h"

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <filesystem>

#ifndef NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR
    #error "NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR must be defined by the build"
#endif

namespace {

nucleus::config_space server_space()
{
    nucleus::config_space_builder builder;
    nucleus::shapes::declare_keyed_server_routes(builder);
    return builder.build();
}

nucleus::source_handle xml_of(const std::string &text)
{
    return nucleus::source_handle(
            nucleus::xml_source::from(nucleus::xml_source_options::of_string(text)));
}

std::string server_document()
{
    return "<cluster>"
           "<server name=\"primary\">"
           "<route><port>80</port><method>get</method></route>"
           "<route><port>443</port><method>post</method></route>"
           "</server>"
           "</cluster>";
}

nucleus::load_result load_document(const nucleus::config_space &space,
                                   bool                         with_empty_layer)
{
    nucleus::load_options opts;
    opts.selection = "primary";
    if(with_empty_layer)
        return nucleus::load_config(space,
                                    nucleus::source_stack{xml_of(server_document()),
                                                          nucleus::shapes::runtime_layer({})},
                                    opts);
    return nucleus::load_config(space,
                                nucleus::source_stack{xml_of(server_document())}, opts);
}

nucleus::load_result load_wide_extend(const nucleus::config_space &space)
{
    const std::filesystem::path root(NUCLEUS_COLLECTION_SHAPES_FIXTURE_DIR);
    REQUIRE(std::filesystem::is_directory(root / "strain_wide_extend"));

    nucleus::load_options opts;
    opts.document_paths = {"derived.xml"};
    opts.make_document  = nucleus::shapes::file_factory(
            (root / "strain_wide_extend").string());
    opts.selection = "primary";
    return nucleus::load_config(space, nucleus::source_stack{}, opts);
}

void require_direct_overlay(const nucleus::config &cfg)
{
    const std::string serialized = nucleus::shapes::serialize(cfg);
    INFO(serialized);
    REQUIRE(cfg.get("cluster/server/route[0]/method") == "patch");
    REQUIRE(cfg.get("cluster/server/route[0]/port") == "80");
    REQUIRE(cfg.get("cluster/server/route[1]/method") == "post");
    REQUIRE(cfg.get("cluster/server/route[1]/port") == "443");
    REQUIRE(serialized.find(
                    "cluster/server/route[1]/method = post [0|stack[0]|anonymous|-]\n") != std::string::npos);
    REQUIRE(serialized.find(
                    "cluster/server/route[1]/port = 443 [0|stack[0]|anonymous|-]\n") != std::string::npos);
    REQUIRE(cfg.get("cluster/server/name") == "primary");
    for(const std::string &key : cfg.keys())
        REQUIRE(key.find("/primary/") == std::string::npos);
}

}

TEST_CASE("a runtime layer addressing one route leaf preserves every leaf and origin "
          "of the untouched sibling route",
          "[collection_shapes][keyed][displacement]")
{
    const nucleus::config_space space = server_space();
    nucleus::load_options       opts;
    opts.selection = "primary";

    const nucleus::load_result loaded = nucleus::load_config(space,
                                                             nucleus::source_stack{
                                                                     xml_of(server_document()),
                                                                     nucleus::shapes::runtime_layer(
                                                                             {{"cluster/server/route[0]/method", "patch"}})},
                                                             opts);
    REQUIRE(loaded);
    require_direct_overlay(loaded.value());
}

TEST_CASE("an empty upper layer leaves a selected strain byte-identical to the "
          "single-document load",
          "[collection_shapes][keyed][displacement]")
{
    const nucleus::config_space space   = server_space();
    const nucleus::load_result  direct  = load_document(space, false);
    const nucleus::load_result  layered = load_document(space, true);
    REQUIRE(direct);
    REQUIRE(layered);

    const std::string direct_tree  = nucleus::shapes::serialize(direct.value());
    const std::string layered_tree = nucleus::shapes::serialize(layered.value());
    INFO(direct_tree);
    INFO(layered_tree);
    REQUIRE(layered_tree == direct_tree);
}

TEST_CASE("a wide extend reopening one strain preserves both leaves and the origin "
          "of its surplus route",
          "[collection_shapes][keyed][displacement]")
{
    const nucleus::config_space space  = server_space();
    const nucleus::load_result  loaded = load_wide_extend(space);
    REQUIRE(loaded);
    const nucleus::config &cfg        = loaded.value();
    const std::string      serialized = nucleus::shapes::serialize(cfg);
    INFO(serialized);

    REQUIRE(cfg.get("cluster/server/route[0]/port") == "8080");
    REQUIRE(cfg.get("cluster/server/route[0]/method") == "put");
    REQUIRE(cfg.get("cluster/server/route[1]/port") == "443");
    REQUIRE(cfg.get("cluster/server/route[1]/method") == "post");
    REQUIRE(serialized.find(
                    "cluster/server/route[1]/port = 443 [0|path:base.xml|anonymous|0]\n") != std::string::npos);
    REQUIRE(serialized.find(
                    "cluster/server/route[1]/method = post [0|path:base.xml|anonymous|0]\n") != std::string::npos);
}

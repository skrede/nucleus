// A shared const space is the stronger claim than distinct spaces, which share
// strictly less. The latch and repeated loads give ThreadSanitizer overlapping
// resolution windows through both collection routes.

#include "concurrent_collection_load_test_support.h"

#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace load_test = nucleus::concurrent_collection_load_test;

namespace {

void declare_nested_collection(nucleus::config_space_builder &builder)
{
    using nucleus::anchor;
    REQUIRE(builder.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(builder.register_element(
            nucleus::element("label", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("route", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::element("port", anchor::keyspace("cluster/node/route"))));
    REQUIRE(builder.register_element(
            nucleus::repeated_element("tags", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
            nucleus::element("name", anchor::keyspace("cluster/node/tags"))));
}

void declare_keyed_collection(nucleus::config_space_builder &builder)
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

nucleus::source_stack ordinal_layers()
{
    nucleus::runtime_source base;
    base.set("cluster/node[0]/label", "keep")
            .set("cluster/node[0]/route[0]/port", "8080")
            .set("cluster/node[0]/route[1]/port", "8443")
            .set("cluster/node[0]/tags[0]/name", "a")
            .set("cluster/node[1]/label", "sibling");
    nucleus::runtime_source overlay;
    overlay.set("cluster/node[0]/route[0]/port", "7000");
    return nucleus::source_stack{std::move(base), std::move(overlay)};
}

nucleus::source_stack keyed_layers()
{
    nucleus::runtime_source overlay;
    overlay.set("cluster/server/route[0]/method", "patch");
    return nucleus::source_stack{std::move(overlay)};
}

nucleus::source_handle keyed_document(const std::string &)
{
    nucleus::runtime_source document;
    document.set("cluster/server/primary/route[0]/port", "80")
            .set("cluster/server/primary/route[0]/method", "get")
            .set("cluster/server/primary/route[1]/port", "443")
            .set("cluster/server/primary/route[1]/method", "post");
    return nucleus::source_handle(std::move(document));
}

nucleus::load_options keyed_options()
{
    nucleus::load_options options;
    options.document_paths = {"base.runtime"};
    options.make_document  = keyed_document;
    options.selection      = "primary";
    return options;
}

}

TEST_CASE("concurrent loads of nested ordinal collections produce identical trees",
          "[concurrent][load][collection_shapes]")
{
    nucleus::config_space_builder builder;
    declare_nested_collection(builder);
    const nucleus::config_space space = nucleus::builder_result_test::built(builder);

    const std::vector<std::string> results = load_test::concurrent_results(
            space, ordinal_layers, {}, 8, 64);
    REQUIRE(results.front().find("cluster/node[0]/route[0]/port = 7000 [1|stack[1]|-") != std::string::npos);
    REQUIRE(results.front().find("cluster/node[0]/route[1]/port = 8443 [0|stack[0]|-") != std::string::npos);
    REQUIRE(results.front().find("cluster/node[1]/label = sibling") != std::string::npos);
}

TEST_CASE("concurrent selected-strain loads resolve keyed collections identically",
          "[concurrent][load][collection_shapes][keyed][sweep]")
{
    nucleus::config_space_builder builder;
    declare_keyed_collection(builder);
    const nucleus::config_space space   = nucleus::builder_result_test::built(builder);
    const nucleus::load_options options = keyed_options();

    constexpr std::size_t thread_counts[] = {2, 4, 8, 16};
    constexpr std::size_t repetitions[]   = {1, 8, 64};
    for(std::size_t thread_count : thread_counts)
        for(std::size_t repetition_count : repetitions)
        {
            CAPTURE(thread_count, repetition_count);
            const std::vector<std::string> results = load_test::concurrent_results(
                    space, keyed_layers, options, thread_count, repetition_count);
            REQUIRE(results.front().find("cluster/server/route[0]/method = patch [1|stack[0]|-") != std::string::npos);
            REQUIRE(results.front().find("cluster/server/route[1]/port = 443 [0|path:base.runtime|0") != std::string::npos);
            REQUIRE(results.front().find("cluster/server/route[1]/method = post [0|path:base.runtime|0") != std::string::npos);
        }
}

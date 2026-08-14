// A shared const space is the stronger claim than distinct spaces, which share
// strictly less. The latch and repeated loads give ThreadSanitizer overlapping
// resolution windows through both collection routes.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <latch>
#include <string>
#include <thread>
#include <vector>
#include <cstddef>
#include <utility>
#include <functional>

namespace {

using source_factory = nucleus::source_stack (*)();

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

std::string serialize(const nucleus::config &config)
{
    std::string out;
    for(const std::string &key : config.keys())
    {
        const nucleus::origin *origin = config.provenance_of(key);
        out += key + " = " + config.get(key).value_or(std::string()) + " [";
        if(origin != nullptr)
            out += std::to_string(origin->rank) + "|" + origin->layer + "|" + (origin->inheritance_layer.has_value() ? std::to_string(origin->inheritance_layer.value()) : std::string("-"));
        out += "]\n";
    }
    return out;
}

void load_repeatedly(const nucleus::config_space &space, source_factory make_sources,
                     const nucleus::load_options &options, std::size_t repetitions,
                     std::latch &start, std::string &out, char &ok)
{
    start.arrive_and_wait();
    for(std::size_t repetition = 0; repetition < repetitions; ++repetition)
    {
        const nucleus::load_result loaded =
                nucleus::load_config(space, make_sources(), options);
        if(!loaded)
            return;
        out = serialize(loaded.value());
    }
    ok = 1;
}

std::vector<std::string> concurrent_results(
        const nucleus::config_space &space, source_factory make_sources,
        const nucleus::load_options &options, std::size_t thread_count,
        std::size_t repetitions)
{
    std::vector<std::string> results(thread_count);
    std::vector<char>        ok(thread_count, 0);
    std::latch               start(static_cast<std::ptrdiff_t>(thread_count));
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for(std::size_t index = 0; index < thread_count; ++index)
        threads.emplace_back(load_repeatedly, std::cref(space), make_sources,
                             std::cref(options), repetitions, std::ref(start),
                             std::ref(results[index]), std::ref(ok[index]));
    for(std::thread &thread : threads)
        thread.join();
    for(char status : ok)
        REQUIRE(status);
    for(std::size_t index = 1; index < thread_count; ++index)
        REQUIRE(results[index] == results.front());
    return results;
}

}

TEST_CASE("concurrent loads of nested ordinal collections produce identical trees",
          "[concurrent][load][collection_shapes]")
{
    nucleus::config_space_builder builder;
    declare_nested_collection(builder);
    const nucleus::config_space space = builder.build();

    const std::vector<std::string> results = concurrent_results(
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
    const nucleus::config_space space   = builder.build();
    const nucleus::load_options options = keyed_options();

    constexpr std::size_t thread_counts[] = {2, 4, 8, 16};
    constexpr std::size_t repetitions[]   = {1, 8, 64};
    for(std::size_t thread_count : thread_counts)
        for(std::size_t repetition_count : repetitions)
        {
            CAPTURE(thread_count, repetition_count);
            const std::vector<std::string> results = concurrent_results(
                    space, keyed_layers, options, thread_count, repetition_count);
            REQUIRE(results.front().find("cluster/server/route[0]/method = patch [1|stack[0]|-") != std::string::npos);
            REQUIRE(results.front().find("cluster/server/route[1]/port = 443 [0|path:base.runtime|0") != std::string::npos);
            REQUIRE(results.front().find("cluster/server/route[1]/method = post [0|path:base.runtime|0") != std::string::npos);
        }
}

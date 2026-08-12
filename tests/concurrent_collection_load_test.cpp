// The collection sweep keeps its per-layer state -- the leaf ordinal counters, the
// swept-instance set and the bucketed instance scan -- inside fold()'s layer loop
// (resolution_context.h:252-276). concurrent_load_test.cpp already pins race freedom
// for a flat schema, but a flat schema never reaches that loop's collection path, so
// none of those structures is exercised there. This drives the same shared-const-space
// design through nested repeated containers with a layer that replaces one innermost
// instance -- the shape that fills all of them.
//
// A shared const space is the stronger claim than the distinct-space one: distinct
// spaces share strictly less. ASan cannot see data races; the ThreadSanitizer CI
// flavor running this same test is what validates the claim. The latch and the
// iteration loop exist to give TSan genuinely overlapping access windows rather than
// threads that serialize through their own setup.

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
#include <utility>
#include <cstddef>

namespace {

constexpr std::size_t thread_count = 8;
constexpr std::size_t iterations = 64;

// cluster / node[] / { port, label, route[]/port, tags[]/name } -- route and tags are
// repeated containers nested inside a repeated container, so an addressed route
// instance has both a sibling instance and a sibling subtree to leave alone.
void declare_nested_collection(nucleus::config_space_builder &builder)
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
        nucleus::repeated_element("tags", anchor::keyspace("cluster/node"))));
    REQUIRE(builder.register_element(
        nucleus::element("name", anchor::keyspace("cluster/node/tags"))));
}

nucleus::source_stack layered_instances()
{
    nucleus::runtime_source base;
    base.set("cluster/node[0]/port", "80")
        .set("cluster/node[0]/label", "keep")
        .set("cluster/node[0]/route[0]/port", "8080")
        .set("cluster/node[0]/route[1]/port", "8443")
        .set("cluster/node[0]/tags[0]/name", "a")
        .set("cluster/node[1]/port", "90");

    // Addresses route[0] only: the sweep must replace that instance wholly and spare
    // route[1], node[0]'s own leaves and the nested tags subtree.
    nucleus::runtime_source overlay;
    overlay.set("cluster/node[0]/route[0]/port", "7000");

    return nucleus::source_stack{std::move(base), std::move(overlay)};
}

std::map<std::string, std::string> snapshot_of(const nucleus::config &cfg)
{
    std::map<std::string, std::string> out;
    for(const std::string &key : cfg.keys())
        out.emplace(key, cfg.get(key).value_or(std::string{}));
    return out;
}

// Catch2's assertion macros are not thread-safe, so a worker reports failure by
// leaving its ok flag clear and the caller asserts once every thread has joined.
void load_repeatedly(const nucleus::config_space &space, std::latch &start,
                     std::map<std::string, std::string> &out, char &ok)
{
    start.arrive_and_wait();
    for(std::size_t iter = 0; iter < iterations; ++iter)
    {
        const nucleus::load_result loaded =
            nucleus::load_config(space, layered_instances(), {});
        if(!loaded)
            return;
        out = snapshot_of(loaded.value());
    }
    ok = 1;
}

}

TEST_CASE("N threads load one shared const space of nested repeated containers "
          "lock-free with identical results",
          "[concurrent][load][collection_shapes]")
{
    nucleus::config_space_builder builder;
    declare_nested_collection(builder);
    const nucleus::config_space space = builder.build();

    std::vector<std::map<std::string, std::string>> results(thread_count);
    // char, not vector<bool>: the bit-packed specialization shares machine words
    // across indices, so concurrent per-thread writes would race on the same word.
    std::vector<char> ok(thread_count, 0);
    std::latch start(thread_count);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for(std::size_t i = 0; i < thread_count; ++i)
        threads.emplace_back(load_repeatedly, std::cref(space), std::ref(start),
                             std::ref(results[i]), std::ref(ok[i]));
    for(std::thread &t : threads)
        t.join();

    for(std::size_t i = 0; i < thread_count; ++i)
        REQUIRE(ok[i]);

    const std::map<std::string, std::string> &expected = results.front();
    REQUIRE(expected.at("cluster/node[0]/route[0]/port") == "7000");
    REQUIRE(expected.at("cluster/node[0]/route[1]/port") == "8443");
    REQUIRE(expected.at("cluster/node[0]/label") == "keep");
    REQUIRE(expected.at("cluster/node[0]/tags[0]/name") == "a");
    REQUIRE(expected.at("cluster/node[1]/port") == "90");
    for(std::size_t i = 1; i < thread_count; ++i)
        REQUIRE(results[i] == expected);
}

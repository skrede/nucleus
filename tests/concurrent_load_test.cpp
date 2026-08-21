// Concurrent loads on one shared const config_space need no synchronization:
// load borrows the space's registries by const reference and owns all
// mutable resolve state on its own stack. N threads call it on the SAME const space
// with no mutex; all succeed with byte-identical results. ASan cannot see data
// races -- the race-freedom claim is validated by the ThreadSanitizer CI job
// running this same test; the latch and the iteration loop exist to give TSan
// genuinely overlapping access windows rather than threads that serialize
// through their own setup.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <latch>
#include <vector>
#include <thread>
#include <string>
#include <cstddef>

using nucleus::anchor;

namespace {

struct concurrent_run
{
    // char, not vector<bool>: the bit-packed specialization shares machine words
    // across indices, so concurrent per-thread writes would race on the same word.
    std::vector<std::map<std::string, std::string>> results;
    std::vector<char> ok;
};

void load_repeatedly(const nucleus::config_space &space, std::size_t iterations,
                     std::latch &start, std::map<std::string, std::string> &out, char &ok)
{
    start.arrive_and_wait();
    // Borrow the shared space by const reference -- NO mutex anywhere. Each
    // thread owns its source and stack on its own stack so nothing mutable
    // is shared; the feeder declares nesting so the auto-gate admits it.
    std::map<std::string, std::string> snapshot;
    for(std::size_t iter = 0; iter < iterations; ++iter)
    {
        nucleus::runtime_source src;
        src.set("server/host", "localhost").set("server/port", "8080");
        auto loaded = nucleus::load_config(space,
            nucleus::source_stack{std::move(src)},
            {});
        if(!loaded)
            return;
        snapshot.clear();
        for(const std::string &key : loaded.value().keys())
            snapshot.emplace(key, loaded.value().get(key).value_or(std::string{}));
    }
    out = std::move(snapshot);
    ok = 1;
}

// Every thread arrives at the latch before any thread touches the shared space, so the
// loads genuinely overlap instead of serializing through per-thread setup.
concurrent_run load_from_threads(const nucleus::config_space &space,
                                 std::size_t thread_count, std::size_t iterations)
{
    concurrent_run run{std::vector<std::map<std::string, std::string>>(thread_count),
                       std::vector<char>(thread_count, 0)};
    std::latch start(static_cast<std::ptrdiff_t>(thread_count));
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for(std::size_t i = 0; i < thread_count; ++i)
        threads.emplace_back([&space, &run, &start, i, iterations]() {
            load_repeatedly(space, iterations, start, run.results[i], run.ok[i]);
        });
    for(std::thread &t : threads)
        t.join();
    return run;
}

}

TEST_CASE("N threads load one shared const space lock-free with identical results",
          "[concurrent][load]")
{
    nucleus::config_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::element("port", anchor::keyspace("server"))));
    const nucleus::config_space space = builder.build();

    constexpr std::size_t thread_count = 8;
    const concurrent_run run = load_from_threads(space, thread_count, 64);

    // Every thread succeeded.
    for(std::size_t i = 0; i < thread_count; ++i)
        REQUIRE(run.ok[i]);

    // Every resolved config is byte-identical to the first.
    const std::map<std::string, std::string> &expected = run.results.front();
    REQUIRE(expected.at("server/host") == "localhost");
    REQUIRE(expected.at("server/port") == "8080");
    for(std::size_t i = 1; i < thread_count; ++i)
        REQUIRE(run.results[i] == expected);
}

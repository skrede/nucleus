#ifndef HPP_GUARD_NUCLEUS_TESTS_RUNTIME_CONCURRENT_COLLECTION_LOAD_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_RUNTIME_CONCURRENT_COLLECTION_LOAD_TEST_SUPPORT_H

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/config_source/source_stack.h"

#include <catch2/catch_test_macros.hpp>

#include <latch>
#include <string>
#include <thread>
#include <vector>
#include <cstddef>
#include <functional>

namespace nucleus::concurrent_collection_load_test {

using source_factory = nucleus::source_stack (*)();

inline std::string serialize(const nucleus::config &config)
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

inline void load_repeatedly(const nucleus::config_space &space, source_factory make_sources,
                            const nucleus::load_options &options, std::size_t repetitions,
                            const std::string &expected, std::latch &start,
                            std::string &out, char &ok)
{
    start.arrive_and_wait();
    for(std::size_t repetition = 0; repetition < repetitions; ++repetition)
    {
        const nucleus::load_result loaded =
                nucleus::load_config(space, make_sources(), options);
        if(!loaded)
            return;
        out = serialize(loaded.value());
        if(out != expected)
            return;
    }
    ok = 1;
}

inline void run_loaders(const nucleus::config_space &space, source_factory make_sources,
                        const nucleus::load_options &options, std::size_t thread_count,
                        std::size_t repetitions, const std::string &expected,
                        std::vector<std::string> &results, std::vector<char> &ok)
{
    std::latch               start(static_cast<std::ptrdiff_t>(thread_count));
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for(std::size_t index = 0; index < thread_count; ++index)
        threads.emplace_back(load_repeatedly, std::cref(space), make_sources,
                             std::cref(options), repetitions, std::cref(expected),
                             std::ref(start), std::ref(results[index]),
                             std::ref(ok[index]));
    for(std::thread &thread : threads)
        thread.join();
}

inline std::vector<std::string> concurrent_results(
        const nucleus::config_space &space, source_factory make_sources,
        const nucleus::load_options &options, std::size_t thread_count,
        std::size_t repetitions)
{
    const nucleus::load_result reference =
            nucleus::load_config(space, make_sources(), options);
    REQUIRE(reference);
    const std::string        expected = serialize(reference.value());
    std::vector<std::string> results(thread_count);
    std::vector<char>        ok(thread_count, 0);
    run_loaders(space, make_sources, options, thread_count, repetitions, expected,
                results, ok);
    for(std::size_t index = 0; index < thread_count; ++index)
    {
        INFO("thread " << index << ": " << results[index]);
        REQUIRE(ok[index]);
    }
    return results;
}

}

#endif

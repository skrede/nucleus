// Concurrent loads on one shared const configuration_space need no synchronization:
// load_configuration borrows the space's registries by const reference and owns all
// mutable resolve state on its own stack. N threads call it on the SAME const space
// with no mutex; all succeed with byte-identical results. Under ASan this exercises
// the shared-const-read design with no data race.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/entry/configuration.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <vector>
#include <thread>
#include <string>
#include <cstddef>

using nucleus::anchor;

namespace {

// A self-contained env layer (no borrowed source needed across threads): each
// thread builds its OWN options on its own stack so nothing mutable is shared.
nucleus::source_stack_options thread_options()
{
    nucleus::source_stack_options opts;
    opts.env = nucleus::env_source_options{{
        {"server/host", "localhost"},
        {"server/port", "8080"},
    }};
    return opts;
}

}

TEST_CASE("N threads load one shared const space lock-free with identical results",
          "[concurrent][load]")
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", anchor::root()));
    builder.register_element(nucleus::element("host", anchor::keyspace("server")));
    builder.register_element(nucleus::element("port", anchor::keyspace("server")));
    const nucleus::configuration_space space = builder.build();

    constexpr std::size_t thread_count = 8;
    std::vector<std::map<std::string, std::string>> results(thread_count);
    std::vector<bool> ok(thread_count, false);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for(std::size_t i = 0; i < thread_count; ++i)
    {
        threads.emplace_back([&space, &results, &ok, i]() {
            // Borrow the shared space by const reference -- NO mutex anywhere.
            const nucleus::source_stack_options opts = thread_options();
            auto loaded = nucleus::load_configuration(space, opts);
            if(!loaded)
                return;
            std::map<std::string, std::string> snapshot;
            for(const std::string &key : loaded.value().keys())
                snapshot.emplace(key, loaded.value().get(key).value_or(std::string{}));
            results[i] = std::move(snapshot);
            ok[i] = true;
        });
    }
    for(std::thread &t : threads)
        t.join();

    // Every thread succeeded.
    for(std::size_t i = 0; i < thread_count; ++i)
        REQUIRE(ok[i]);

    // Every resolved configuration is byte-identical to the first.
    const std::map<std::string, std::string> &expected = results.front();
    REQUIRE(expected.at("server/host") == "localhost");
    REQUIRE(expected.at("server/port") == "8080");
    for(std::size_t i = 1; i < thread_count; ++i)
        REQUIRE(results[i] == expected);
}

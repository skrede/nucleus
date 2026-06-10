// Concurrent loads on one shared const configuration_space need no synchronization:
// load borrows the space's registries by const reference and owns all
// mutable resolve state on its own stack. N threads call it on the SAME const space
// with no mutex; all succeed with byte-identical results. Under ASan this exercises
// the shared-const-read design with no data race.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/configuration.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <vector>
#include <thread>
#include <string>
#include <cstddef>

using nucleus::anchor;


TEST_CASE("N threads load one shared const space lock-free with identical results",
          "[concurrent][load]")
{
    nucleus::configuration_space_builder builder;
    REQUIRE(builder.register_element(nucleus::element("server", anchor::root())));
    REQUIRE(builder.register_element(nucleus::element("host", anchor::keyspace("server"))));
    REQUIRE(builder.register_element(nucleus::element("port", anchor::keyspace("server"))));
    const nucleus::configuration_space space = builder.build();

    constexpr std::size_t thread_count = 8;
    std::vector<std::map<std::string, std::string>> results(thread_count);
    // char, not vector<bool>: the bit-packed specialization shares machine words
    // across indices, so concurrent per-thread writes would race on the same word.
    std::vector<char> ok(thread_count, 0);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for(std::size_t i = 0; i < thread_count; ++i)
    {
        threads.emplace_back([&space, &results, &ok, i]() {
            // Borrow the shared space by const reference -- NO mutex anywhere. Each
            // thread owns its source and stack on its own stack so nothing mutable
            // is shared; the feeder declares nesting so the auto-gate admits it.
            nucleus::runtime_source src;
            src.set("server/host", "localhost").set("server/port", "8080");
            auto loaded = nucleus::load(space,
                nucleus::source_stack{std::move(src)},
                {});
            if(!loaded)
                return;
            std::map<std::string, std::string> snapshot;
            for(const std::string &key : loaded.value().keys())
                snapshot.emplace(key, loaded.value().get(key).value_or(std::string{}));
            results[i] = std::move(snapshot);
            ok[i] = 1;
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

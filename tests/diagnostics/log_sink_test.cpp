#include "nucleus/format.h"
#include "nucleus/log_sink.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <sstream>
#include <utility>

TEST_CASE("the default sink is a no-op", "[log_sink]")
{
    nucleus::log_sink sink;
    // Calling through the default seam must not throw or observe anything.
    REQUIRE_NOTHROW(sink.log(nucleus::log_level::error, "ignored"));
}

TEST_CASE("a host bridges the seam via a callable", "[log_sink]")
{
    std::vector<std::pair<nucleus::log_level, std::string>> captured;
    nucleus::log_sink_f sink(
        [&](nucleus::log_level level, std::string_view message)
        {
            captured.emplace_back(level, std::string(message));
        });

    nucleus::log_sink &seam = sink;
    seam.log(nucleus::log_level::warn, nucleus::format("missing key: {}", "a/b/c"));

    REQUIRE(captured.size() == 1);
    REQUIRE(captured[0].first == nucleus::log_level::warn);
    REQUIRE(captured[0].second == "missing key: a/b/c");
}

TEST_CASE("a host bridges the seam to an ostream", "[log_sink]")
{
    std::ostringstream out;
    nucleus::log_sink_s sink(out);

    nucleus::log_sink &seam = sink;
    seam.log(nucleus::log_level::info, "ready");

    REQUIRE(out.str() == "[info] ready\n");
}

TEST_CASE("level names are stable", "[log_sink]")
{
    REQUIRE(nucleus::to_string(nucleus::log_level::trace) == "trace");
    REQUIRE(nucleus::to_string(nucleus::log_level::error) == "error");
}

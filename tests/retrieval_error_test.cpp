#include "nucleus/config.h"

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <map>
#include <array>
#include <string>
#include <cstdint>
#include <utility>

namespace {

constexpr std::array<std::size_t, 4> ordinals{0, 1, 2, 10};

std::string node_path(std::size_t ordinal)
{
    return "cluster/node[" + std::to_string(ordinal) + "]";
}

std::string route_path(std::size_t outer, std::size_t inner)
{
    return node_path(outer) + "/route[" + std::to_string(inner) + "]";
}

nucleus::config repeated_values()
{
    std::map<std::string, std::string> raw;
    std::map<std::string, std::any>    typed;
    for(std::size_t const outer : ordinals)
    {
        raw.emplace(node_path(outer) + "/label", "node");
        for(std::size_t const inner : ordinals)
        {
            const std::string path  = route_path(outer, inner) + "/port";
            const auto        value = static_cast<std::int32_t>((outer * 100) + inner);
            raw.emplace(path, std::to_string(value));
            typed.emplace(path, value);
        }
    }
    return nucleus::config(
            std::move(raw), std::move(typed), nucleus::provenance{});
}

void check_crossing(const nucleus::config &config, const std::string &query,
                    const std::string &container)
{
    auto result = config.get_as<std::int32_t>(query);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == nucleus::errc::index_required);
    CHECK(result.error().message.find("'" + container + "'") != std::string::npos);
    CHECK(result.error().message.find("4 instance(s)") != std::string::npos);
}

}

TEST_CASE("raw repeated values without typed entries report a missing converter",
          "[retrieval][error]")
{
    const nucleus::config config  = repeated_values();
    auto                  untyped = config.get_all_as<std::int32_t>("cluster/node/label");
    REQUIRE_FALSE(untyped);
    CHECK(untyped.error().code == nucleus::errc::missing_converter);

    auto absent = config.get_all_as<std::int32_t>("cluster/node/missing");
    REQUIRE_FALSE(absent);
    CHECK(absent.error().code == nucleus::errc::absent_key);
}

TEST_CASE("typed scalar reads preserve an explicit outer ordinal",
          "[retrieval][error]")
{
    check_crossing(repeated_values(), "cluster/node[2]/route/port",
                   "cluster/node[2]/route");
}

TEST_CASE("typed scalar reads report the first omitted repeated segment",
          "[retrieval][error]")
{
    check_crossing(repeated_values(), "cluster/node/route/port",
                   "cluster/node");
}

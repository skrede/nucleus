#include "xml_repeated_test_support.h"

#include "nucleus/keyspace/entry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

namespace test = nucleus::xml_repeated_test;

namespace {

std::vector<std::string> paths_of(
        const std::vector<nucleus::keyspace_entry> &entries)
{
    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for(const auto &entry : entries)
        paths.push_back(entry.path);
    return paths;
}

void check_text(const std::vector<nucleus::keyspace_entry> &entries,
                const std::string                          &path,
                const std::string                          &expected)
{
    for(const auto &entry : entries)
        if(entry.path == path)
            REQUIRE(std::string(entry.value.text()) == expected);
}

}

TEST_CASE("xml ordinal emission -- N node instances", "[xml][repeated_container][ordinal]")
{
    nucleus::schema_registry   reg  = test::cluster_nodes_registry();
    nucleus::schema_projection proj = reg.projection();
    auto                       src  = test::xml_of(
            "<cluster>"
            "<node><port>80</port></node>"
            "<node><port>90</port></node>"
            "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    const std::vector<std::string> paths = paths_of(entries);

    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/node[0]/port") != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/node[1]/port") != paths.end());
    const auto it0 = std::find(paths.begin(), paths.end(), "cluster/node[0]/port");
    const auto it1 = std::find(paths.begin(), paths.end(), "cluster/node[1]/port");
    REQUIRE(it0 < it1);
    check_text(entries, "cluster/node[0]/port", "80");
    check_text(entries, "cluster/node[1]/port", "90");
}

TEST_CASE("xml nested ordinal emission", "[xml][repeated_container][nested]")
{
    nucleus::schema_registry   reg  = test::cluster_nodes_routes_registry();
    nucleus::schema_projection proj = reg.projection();
    auto                       src  = test::xml_of(
            "<cluster>"
            "<node>"
            "<route><method>fast</method></route>"
            "<route><method>slow</method></route>"
            "</node>"
            "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto                    &entries = result.value().entries;
    const std::vector<std::string> paths   = paths_of(entries);

    REQUIRE(std::find(paths.begin(), paths.end(),
                      "cluster/node[0]/route[0]/method") != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(),
                      "cluster/node[0]/route[1]/method") != paths.end());
    check_text(entries, "cluster/node[0]/route[0]/method", "fast");
    check_text(entries, "cluster/node[0]/route[1]/method", "slow");
}

TEST_CASE("non-repeated container -- no ordinals assigned", "[xml][repeated_container][plain]")
{
    nucleus::schema_registry   reg  = test::cluster_plain_server_registry();
    nucleus::schema_projection proj = reg.projection();

    auto src = test::xml_of(
            "<cluster>"
            "<server><port>80</port></server>"
            "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    const std::vector<std::string> paths = paths_of(entries);

    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/server/port") != paths.end());
    for(const auto &p : paths)
        REQUIRE(p.find('[') == std::string::npos);
}

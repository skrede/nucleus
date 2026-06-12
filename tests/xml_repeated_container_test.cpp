// Ordinal emission for repeated-container instances: xml_source assigns zero-based
// ordinals in document order to sibling elements under a repeated schema container.
// Tests verify the raw batch entries emitted by xml_source::pull() (the fold is not
// yet aware of indexed paths; that is Plan 05).

#include "nucleus/xml/xml_source.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using nucleus::anchor;
using nucleus::schema_registry;

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

// Schema: cluster -> node (repeated) -> port (leaf)
schema_registry cluster_nodes_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace("cluster/node"))));
    return reg;
}

// Schema: cluster -> node (repeated) -> route (repeated) -> method (leaf)
schema_registry cluster_nodes_routes_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::repeated_element("route", anchor::keyspace("cluster/node"))));
    REQUIRE(reg.attach(nucleus::element("method", anchor::keyspace("cluster/node/route"))));
    return reg;
}

// Schema: cluster -> server (plain, non-repeated) -> port (leaf)
schema_registry cluster_plain_server_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("server", anchor::keyspace("cluster"))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace("cluster/server"))));
    return reg;
}

}

TEST_CASE("xml ordinal emission -- N node instances", "[xml][repeated_container][ordinal]")
{
    schema_registry reg = cluster_nodes_registry();
    nucleus::schema_projection proj = reg.projection();

    auto src = xml_of(
        "<cluster>"
        "<node><port>80</port></node>"
        "<node><port>90</port></node>"
        "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for(const auto &e : entries)
        paths.push_back(e.path);

    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/node[0]/port") != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/node[1]/port") != paths.end());

    // Ordinals are zero-based and in document order: [0] before [1].
    auto it0 = std::find(paths.begin(), paths.end(), "cluster/node[0]/port");
    auto it1 = std::find(paths.begin(), paths.end(), "cluster/node[1]/port");
    REQUIRE(it0 < it1);

    // Values match document order.
    for(const auto &e : entries)
    {
        if(e.path == "cluster/node[0]/port")
            REQUIRE(std::string(e.value.text()) == "80");
        if(e.path == "cluster/node[1]/port")
            REQUIRE(std::string(e.value.text()) == "90");
    }
}

TEST_CASE("xml nested ordinal emission", "[xml][repeated_container][nested]")
{
    schema_registry reg = cluster_nodes_routes_registry();
    nucleus::schema_projection proj = reg.projection();

    auto src = xml_of(
        "<cluster>"
        "<node>"
        "<route><method>fast</method></route>"
        "<route><method>slow</method></route>"
        "</node>"
        "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for(const auto &e : entries)
        paths.push_back(e.path);

    // Nested repetition composes: node[0]/route[0]/... and node[0]/route[1]/...
    REQUIRE(std::find(paths.begin(), paths.end(),
                      "cluster/node[0]/route[0]/method") != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(),
                      "cluster/node[0]/route[1]/method") != paths.end());

    for(const auto &e : entries)
    {
        if(e.path == "cluster/node[0]/route[0]/method")
            REQUIRE(std::string(e.value.text()) == "fast");
        if(e.path == "cluster/node[0]/route[1]/method")
            REQUIRE(std::string(e.value.text()) == "slow");
    }
}

TEST_CASE("non-repeated container -- no ordinals assigned", "[xml][repeated_container][plain]")
{
    schema_registry reg = cluster_plain_server_registry();
    nucleus::schema_projection proj = reg.projection();

    auto src = xml_of(
        "<cluster>"
        "<server><port>80</port></server>"
        "</cluster>");
    src.apply_projection(proj);

    auto result = src.pull();
    REQUIRE(result);
    const auto &entries = result.value().entries;

    std::vector<std::string> paths;
    paths.reserve(entries.size());
    for(const auto &e : entries)
        paths.push_back(e.path);

    // Plain non-repeated container: no ordinal suffix.
    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/server/port") != paths.end());

    // No indexed paths produced for a plain container.
    for(const auto &p : paths)
        REQUIRE(p.find('[') == std::string::npos);
}

#include "nucleus/completion/completion_model.h"
#include "nucleus/completion/completion_projection.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>

using nucleus::anchor;
using nucleus::completion_model;
using nucleus::completion_option;
using nucleus::key_path;
using nucleus::schema_registry;

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

schema_registry single_crossing_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::element("endpoint", anchor::keyspace(path_of("cluster/node")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node/endpoint")))));
    return reg;
}

schema_registry nested_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::repeated_element("route", anchor::keyspace(path_of("cluster/node")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node/route")))));
    return reg;
}

schema_registry root_node_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("node")))));
    return reg;
}

schema_registry cluster_node_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("cluster", anchor::root())));
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::keyspace(path_of("cluster")))));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("cluster/node")))));
    return reg;
}

schema_registry plain_registry()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("plexus", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("plexus")))));
    return reg;
}

key_path optional_path(const char *text) { return *text ? path_of(text) : key_path{}; }

struct ordinal_row
{
    const char              *anchor;
    const char              *space;
    const char              *delimiter;
    std::string              flag;
    std::vector<std::size_t> slots;
    std::vector<std::string> pieces;
};

completion_option option_named(const completion_model &model, const std::string &flag)
{
    const auto it = std::ranges::find(model.options, flag, &completion_option::flag);
    REQUIRE(it != model.options.end());
    return *it;
}

void check_rows(const schema_registry &reg, const std::vector<ordinal_row> &rows)
{
    for(const ordinal_row &row : rows)
    {
        DYNAMIC_SECTION(row.flag)
        {
            const auto delimiter = nucleus::cli_delimiter::parse(row.delimiter);
            REQUIRE(delimiter.has_value());
            const completion_model model = nucleus::detail::completion_projection(
                    reg, "myapp", *delimiter, optional_path(row.anchor), optional_path(row.space));
            const completion_option ordinal = option_named(model, row.flag);
            REQUIRE(ordinal.ordinal_slots == row.slots);
            REQUIRE(nucleus::ordinal_pieces(ordinal) == row.pieces);
        }
    }
}

}

TEST_CASE("a single crossing records the offset of its ordinal placeholder",
          "[completion][projection]")
{
    const completion_model model = nucleus::detail::completion_projection(single_crossing_registry(), "myapp", {}, {}, {});

    const completion_option ordinal = option_named(model, "--cluster-node-*-endpoint-port");
    REQUIRE(ordinal.ordinal_slots == std::vector<std::size_t>{15});
    REQUIRE(nucleus::ordinal_pieces(ordinal) == std::vector<std::string>{"--cluster-node-", "-endpoint-port"});

    const completion_option plain = option_named(model, "--cluster-node-endpoint-port");
    REQUIRE(plain.ordinal_slots.empty());
    REQUIRE(nucleus::ordinal_pieces(plain) == std::vector<std::string>{"--cluster-node-endpoint-port"});
}

TEST_CASE("a model reports whether any option carries an ordinal slot",
          "[completion][projection]")
{
    REQUIRE(nucleus::has_ordinal_options(nucleus::detail::completion_projection(single_crossing_registry(), "myapp", {}, {}, {})));
    REQUIRE_FALSE(nucleus::has_ordinal_options(nucleus::detail::completion_projection(plain_registry(), "myapp", {}, {}, {})));
}

TEST_CASE("every repeated dimension a path crosses gets its own slot",
          "[completion][projection]")
{
    check_rows(nested_registry(),
               {{"", "", "-", "--cluster-node-*-route-*-port", {15, 23}, {"--cluster-node-", "-route-", "-port"}},
                {"cluster", "", "-", "--node-*-route-*-port", {7, 15}, {"--node-", "-route-", "-port"}},
                {"", "", "__", "--cluster__node__*__route__*__port", {17, 27}, {"--cluster__node__", "__route__", "__port"}},
                {"cluster/node/route", "", "-", "--*-port", {2}, {"--", "-port"}}});
}

TEST_CASE("a space name's segments carry no slot", "[completion][projection]")
{
    check_rows(root_node_registry(), {{"", "a/7", "-", "--a-7-node-*-port", {11}, {"--a-7-node-", "-port"}}});
}

TEST_CASE("slots are positions, so a delimiter carrying the placeholder byte stays literal",
          "[completion][projection]")
{
    check_rows(cluster_node_registry(),
               {{"", "", "*", "--cluster*node***port", {15}, {"--cluster*node*", "*port"}}});
}

TEST_CASE("a repeated anchor leads the relative flag with its ordinal", "[completion][projection]")
{
    check_rows(cluster_node_registry(),
               {{"cluster/node", "", "-", "--*-port", {2}, {"--", "-port"}},
                {"cluster/node", "a", "-", "--a-*-port", {4}, {"--a-", "-port"}}});
}

TEST_CASE("an anchor below the only crossing projects no ordinal option", "[completion][projection]")
{
    const completion_model model = nucleus::detail::completion_projection(
            single_crossing_registry(), "myapp", {}, path_of("cluster/node/endpoint"), {});
    REQUIRE_FALSE(nucleus::has_ordinal_options(model));
}

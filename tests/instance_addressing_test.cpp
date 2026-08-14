#include "nucleus/config.h"
#include "nucleus/config_node.h"
#include "nucleus/config_space.h"

#include "nucleus/query/query.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <optional>
#include <algorithm>

using namespace nucleus;

namespace {

constexpr std::array<std::size_t, 4> ordinals{0, 1, 2, 10};
config                               make_config()
{
    std::map<std::string, std::string> values;
    for(std::size_t const outer : ordinals)
        for(std::size_t const inner : ordinals)
            values.emplace("cluster/node[" + std::to_string(outer) + "]/route[" + std::to_string(inner) + "]/port", "80");
    values.emplace("mount_a/plugin/logging/sinks/console", "on");
    values.emplace("mount_b/extension/logging/sinks/console", "on");
    auto made = config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}
std::vector<std::string> paths(const std::vector<config_node> &nodes)
{
    std::vector<std::string> result;
    for(const config_node &node : nodes)
        result.emplace_back(node.path());
    return result;
}
std::vector<std::string> paths(selector selected)
{
    return paths(selected.collect());
}
std::vector<std::string> expected_outer_tree(std::size_t outer)
{
    const std::string        node = "cluster/node[" + std::to_string(outer) + "]";
    std::vector<std::string> result{node, node + "/route"};
    for(std::size_t const inner : ordinals)
    {
        const std::string route = node + "/route[" + std::to_string(inner) + "]";
        result.insert(result.end(), {route, route + "/port"});
    }
    return result;
}
std::vector<std::string> expected_node_tree()
{
    std::vector<std::string> result{"cluster/node"};
    for(std::size_t const outer : ordinals)
    {
        const auto subtree = expected_outer_tree(outer);
        result.insert(result.end(), subtree.begin(), subtree.end());
    }
    return result;
}
std::vector<std::string> expected_routes(std::optional<std::size_t> outer_filter,
                                         std::optional<std::size_t> inner_filter)
{
    std::vector<std::string> result;
    for(std::size_t const outer : ordinals)
    {
        if(outer_filter && outer != *outer_filter)
            continue;
        const std::string root = "cluster/node[" + std::to_string(outer) + "]/route";
        if(!inner_filter)
            result.push_back(root);
        for(std::size_t const inner : ordinals)
            if(!inner_filter || inner == *inner_filter)
            {
                const std::string route = root + "[" + std::to_string(inner) + "]";
                result.insert(result.end(), {route, route + "/port"});
            }
    }
    return result;
}
std::vector<config_node> next_generation(const std::vector<config_node> &nodes)
{
    std::vector<config_node> result;
    for(const config_node &node : nodes)
    {
        const auto children = node.children();
        result.insert(result.end(), children.begin(), children.end());
    }
    return result;
}
void check_depths(const config_node &anchor, const schema_query_context &ctx)
{
    std::vector<config_node> expected{anchor};
    std::size_t              depth = 0;
    while(!expected.empty())
    {
        CHECK(paths(query(anchor, ctx).at_depth(depth)) == paths(expected));
        expected = next_generation(expected);
        ++depth;
    }
    CHECK(paths(query(anchor, ctx).at_depth(depth)).empty());
    CHECK(paths(query(anchor, ctx).children()) == paths(query(anchor, ctx).at_depth(1)));
}
std::vector<std::string> mounted_paths(const config_node          &anchor,
                                       const schema_query_context &ctx)
{
    std::vector<std::string> result = paths(query(anchor, ctx).under("logging"));
    const std::size_t        prefix = anchor.path().size() + 1;
    for(std::string &path : result)
        path.erase(0, prefix);
    return result;
}
struct pruning_walker final : config_tree_walker
{
    std::vector<std::string> events;
    bool                     enter(const config_node &node) override
    {
        events.push_back("enter:" + std::string(node.path()));
        return node.path() != "cluster/node[0]";
    }
    void leave(const config_node &node) override
    {
        events.push_back("leave:" + std::string(node.path()));
    }
};

}

TEST_CASE("structural selection is relative and inclusive", "[instance_addressing]")
{
    const config cfg = make_config();
    const auto   ctx = config_space_builder{}.build().query_context();
    CHECK(paths(query(cfg.root(), ctx).under("cluster/node")) == expected_node_tree());
    CHECK(paths(query(cfg.root(), ctx).under("cluster/node[2]")) == expected_outer_tree(2));
    CHECK(paths(query(cfg.root()["cluster"]["node"][1], ctx).under("")) == expected_outer_tree(1));
    CHECK(mounted_paths(cfg.root()["mount_a"]["plugin"], ctx) == mounted_paths(cfg.root()["mount_b"]["extension"], ctx));
}
TEST_CASE("visit cancellation and walker pruning remain distinct", "[instance_addressing]")
{
    const config                   cfg      = make_config();
    const config_node              anchor   = cfg.root()["cluster"]["node"];
    const auto                     ctx      = config_space_builder{}.build().query_context();
    const std::vector<std::string> complete = paths(query(anchor, ctx));
    for(std::size_t stop = 0; stop < complete.size(); ++stop)
    {
        std::vector<std::string> visited;
        anchor.visit([&](const config_node &node)
                     {
            visited.emplace_back(node.path());
            return visited.size() != stop + 1; });
        CHECK(visited.size() == stop + 1);
        CHECK(std::equal(visited.begin(), visited.end(), complete.begin()));
    }
    pruning_walker walker;
    anchor.walk(walker);
    REQUIRE(walker.events.size() > 4);
    CHECK(walker.events[0] == "enter:cluster/node");
    CHECK(walker.events[1] == "enter:cluster/node[0]");
    CHECK(walker.events[2] == "leave:cluster/node[0]");
    CHECK(walker.events[3] == "enter:cluster/node[1]");
    CHECK(walker.events.back() == "leave:cluster/node");
}
TEST_CASE("nested omitted and explicit ordinals select exact subtrees", "[instance_addressing]")
{
    const config cfg = make_config();
    const auto   ctx = config_space_builder{}.build().query_context();
    struct selection_case
    {
        std::string                path;
        std::optional<std::size_t> outer;
        std::optional<std::size_t> inner;
    };
    const std::vector<selection_case> cases{{"cluster/node/route", {}, {}},
                                            {"cluster/node[2]/route", 2, {}},
                                            {"cluster/node/route[10]", {}, 10},
                                            {"cluster/node[2]/route[10]", 2, 10}};
    for(const selection_case &test : cases)
        CHECK(paths(query(cfg.root(), ctx).under(test.path)) == expected_routes(test.outer, test.inner));
}
TEST_CASE("structural composition follows left-to-right scope", "[instance_addressing]")
{
    const config      cfg    = make_config();
    const auto        ctx    = config_space_builder{}.build().query_context();
    const config_node plugin = cfg.root()["mount_a"]["plugin"];
    CHECK(paths(query(plugin, ctx).under("logging").at_depth(1)) == std::vector<std::string>{"mount_a/plugin/logging/sinks"});
    CHECK(paths(query(plugin, ctx).at_depth(1).under("logging")) == std::vector<std::string>{"mount_a/plugin/logging"});
}
TEST_CASE("depth follows every observable tree generation", "[instance_addressing]")
{
    const config                   cfg  = make_config();
    const auto                     ctx  = config_space_builder{}.build().query_context();
    const config_node              node = cfg.root()["cluster"]["node"];
    const std::vector<config_node> anchors{cfg.root(), cfg.root()["cluster"], node,
                                           node[2], node[2]["route"], node[2]["route"][10]};
    for(const config_node &anchor : anchors)
        check_depths(anchor, ctx);
}

// config_node cursor: null-view chaining navigation, shape queries, visit(), walk()
// value-semantic cursor entered via config::root()
// Navigation never fails loudly; as<T>() returns expected with full path
// kind(), count(), children(), exists(), path()
// pre-order visit() + enter/leave config_tree_walker
// Repeated instances in numeric ordinal order (correct for N >= 11)

#include "nucleus/config_node.h"
#include "support/builder_result_test_support.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"
#include "nucleus/error.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

using nucleus::anchor;

namespace {

nucleus::xml_source xml_of(const std::string &text)
{
    return nucleus::xml_source::from(nucleus::xml_source_options::of_string(text));
}

// Schema: cluster -> node (repeated container) -> endpoint -> port (leaf)
void declare_cluster_schema(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::element("endpoint", anchor::keyspace("cluster/node"))));
    REQUIRE(engine.register_element(
        nucleus::element("port", anchor::keyspace("cluster/node/endpoint"))));
}

// Schema: cluster -> node (repeated container) -> port (leaf only, no endpoint)
void declare_flat_cluster_schema(nucleus::config_space_builder &engine)
{
    REQUIRE(engine.register_element(nucleus::element("cluster", anchor::root())));
    REQUIRE(engine.register_element(
        nucleus::repeated_element("node", anchor::keyspace("cluster"))));
    REQUIRE(engine.register_element(
        nucleus::element("port", anchor::keyspace("cluster/node"))));
}

nucleus::config load_two_nodes(const std::string &port0, const std::string &port1)
{
    nucleus::config_space_builder engine;
    declare_flat_cluster_schema(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    const std::string xml = "<cluster><node><port>" + port0 + "</port></node>"
                            "<node><port>" + port1 + "</port></node></cluster>";
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{xml_of(xml)}, {});
    REQUIRE(loaded);
    return std::move(loaded).value();
}

// Collector walker: records enter/leave calls in order.
struct recording_walker : nucleus::config_tree_walker
{
    struct event { bool enter; std::string path; };
    std::vector<event> events;

    bool enter(const nucleus::config_node &node) override
    {
        events.push_back({true, std::string(node.path())});
        return true;
    }
    void leave(const nucleus::config_node &node) override
    {
        events.push_back({false, std::string(node.path())});
    }
};

// Walker that stops recursion into repeated instances.
struct no_recurse_walker : nucleus::config_tree_walker
{
    std::vector<std::string> entered;

    bool enter(const nucleus::config_node &node) override
    {
        entered.push_back(std::string(node.path()));
        // Stop recursion into repeated instances (paths contain '[').
        return node.path().find('[') == std::string_view::npos;
    }
    void leave(const nucleus::config_node &) override {}
};

}

// ---------------------------------------------------------------------------
// exists() + root
// ---------------------------------------------------------------------------

TEST_CASE("config_node root exists when config has values", "[config_node][exists]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    const nucleus::config_node root = cfg.root();
    REQUIRE(root.exists());
    REQUIRE(root.path().empty());
}

TEST_CASE("config_node child exists for populated path", "[config_node][exists]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    REQUIRE(cfg.root()["cluster"].exists());
    REQUIRE(cfg.root()["cluster"]["node"].exists());
    REQUIRE(cfg.root()["cluster"]["node"][0].exists());
    REQUIRE(cfg.root()["cluster"]["node"][1].exists());
}

TEST_CASE("config_node absent key does not exist", "[config_node][exists][null_view]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    REQUIRE_FALSE(cfg.root()["nonexistent"].exists());
}

TEST_CASE("config_node null-view chaining propagates invalid state", "[config_node][null_view]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    // Navigation through absent keys stays invalid.
    REQUIRE_FALSE(cfg.root()["nonexistent"]["child"].exists());
    REQUIRE_FALSE(cfg.root()["nonexistent"]["child"]["deep"].exists());
}

TEST_CASE("config_node out-of-range ordinal does not exist", "[config_node][exists]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    // Only instances [0] and [1] exist.
    REQUIRE_FALSE(cfg.root()["cluster"]["node"][5].exists());
}

// ---------------------------------------------------------------------------
// path()
// ---------------------------------------------------------------------------

TEST_CASE("config_node path reflects navigation", "[config_node][path]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    REQUIRE(cfg.root()["cluster"]["node"].path() == "cluster/node");
    REQUIRE(cfg.root()["cluster"]["node"][0].path() == "cluster/node[0]");
    REQUIRE(cfg.root()["cluster"]["node"][1].path() == "cluster/node[1]");
    REQUIRE(cfg.root()["cluster"]["node"][0]["port"].path() == "cluster/node[0]/port");
}

// ---------------------------------------------------------------------------
// kind()
// ---------------------------------------------------------------------------

TEST_CASE("config_node kind: repeated, container, scalar", "[config_node][kind]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");

    REQUIRE(cfg.root()["cluster"]["node"].kind() == nucleus::node_kind::repeated);
    REQUIRE(cfg.root()["cluster"]["node"][0].kind() == nucleus::node_kind::container);
    REQUIRE(cfg.root()["cluster"]["node"][0]["port"].kind() == nucleus::node_kind::scalar);
}

// ---------------------------------------------------------------------------
// count()
// ---------------------------------------------------------------------------

TEST_CASE("config_node count on repeated node equals instance count", "[config_node][count]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    REQUIRE(cfg.root()["cluster"]["node"].count() == 2);
}

TEST_CASE("config_node count on scalar node is 1", "[config_node][count]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    REQUIRE(cfg.root()["cluster"]["node"][0]["port"].count() == 1);
}

TEST_CASE("config_node count on absent node is 0", "[config_node][count]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    REQUIRE(cfg.root()["nonexistent"].count() == 0);
}

// ---------------------------------------------------------------------------
// value() and as<T>()
// ---------------------------------------------------------------------------

TEST_CASE("config_node value() returns string for scalar", "[config_node][value]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    auto v = cfg.root()["cluster"]["node"][0]["port"].value();
    REQUIRE(v.has_value());
    REQUIRE(*v == "80");
}

TEST_CASE("config_node as<string> returns expected with value", "[config_node][as]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    auto r = cfg.root()["cluster"]["node"][0]["port"].as<std::string>();
    REQUIRE(r);
    REQUIRE(*r == "80");
}

TEST_CASE("config_node as<string> on absent key returns absent_key error", "[config_node][as][null_view]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    auto r = cfg.root()["nonexistent"].as<std::string>();
    REQUIRE_FALSE(r);
    REQUIRE(r.error().code == nucleus::errc::absent_key);
    // Full path is present in the message.
    REQUIRE(r.error().message.find("nonexistent") != std::string::npos);
}

TEST_CASE("config_node as<string> on null-view node carries full attempted path", "[config_node][as][null_view]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    auto r = cfg.root()["nonexistent"]["child"].as<std::string>();
    REQUIRE_FALSE(r);
    REQUIRE(r.error().code == nucleus::errc::absent_key);
    REQUIRE(r.error().message.find("nonexistent") != std::string::npos);
}

// ---------------------------------------------------------------------------
// children()
// ---------------------------------------------------------------------------

TEST_CASE("config_node children() on repeated node returns instances", "[config_node][children]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    const auto children = cfg.root()["cluster"]["node"].children();
    REQUIRE(children.size() == 2);
    REQUIRE(children[0].path() == "cluster/node[0]");
    REQUIRE(children[1].path() == "cluster/node[1]");
}

TEST_CASE("config_node children() on container node returns immediate children", "[config_node][children]")
{
    // cluster/node[0] is a container; its child is "port".
    const nucleus::config cfg = load_two_nodes("80", "8080");
    const auto children = cfg.root()["cluster"]["node"][0].children();
    REQUIRE(children.size() == 1);
    REQUIRE(children[0].path() == "cluster/node[0]/port");
}

TEST_CASE("config_node children() on scalar returns empty", "[config_node][children]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");
    const auto children = cfg.root()["cluster"]["node"][0]["port"].children();
    REQUIRE(children.empty());
}

// ---------------------------------------------------------------------------
// visit() -- pre-order depth-first, bool-return stops walk
// ---------------------------------------------------------------------------

TEST_CASE("config_node visit() traverses pre-order depth-first", "[config_node][visit]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");

    std::vector<std::string> visited;
    cfg.root()["cluster"]["node"].visit([&](const nucleus::config_node &n) {
        visited.push_back(std::string(n.path()));
        return true;
    });

    // Pre-order: repeated node first, then node[0] and its children, then node[1].
    REQUIRE(visited.size() >= 3);
    REQUIRE(visited[0] == "cluster/node");
    REQUIRE(visited[1] == "cluster/node[0]");
}

TEST_CASE("config_node visit() returning false stops the walk", "[config_node][visit]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");

    std::vector<std::string> visited;
    cfg.root()["cluster"]["node"].visit([&](const nucleus::config_node &n) {
        visited.push_back(std::string(n.path()));
        return false;  // Stop immediately after the first node.
    });

    REQUIRE(visited.size() == 1);
    REQUIRE(visited[0] == "cluster/node");
}

TEST_CASE("config_node visit() enumerates repeated instances in ordinal order", "[config_node][visit][ordinal]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");

    std::vector<std::string> visited;
    cfg.root()["cluster"]["node"].visit([&](const nucleus::config_node &n) {
        if(n.kind() == nucleus::node_kind::container)
            visited.push_back(std::string(n.path()));
        return true;
    });

    REQUIRE(visited.size() == 2);
    REQUIRE(visited[0] == "cluster/node[0]");
    REQUIRE(visited[1] == "cluster/node[1]");
}

// ---------------------------------------------------------------------------
// N >= 11 numeric ordinal order -- must NOT be lexicographic
// ---------------------------------------------------------------------------

TEST_CASE("config_node N >= 11 instances visit in numeric order not lexicographic",
          "[config_node][visit][ordinal][n_large]")
{
    // Build 12 instances. Lexicographic order of "node[N]" would be:
    //   0, 1, 10, 11, 2, 3, 4, 5, 6, 7, 8, 9  (WRONG)
    // Numeric order is: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11  (CORRECT)
    nucleus::config_space_builder engine;
    declare_flat_cluster_schema(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    std::string xml = "<cluster>";
    for(int i = 0; i < 12; ++i)
        xml += "<node><port>" + std::to_string(i * 100) + "</port></node>";
    xml += "</cluster>";

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{xml_of(xml)}, {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    // children() must be in numeric order.
    const auto children = cfg.root()["cluster"]["node"].children();
    REQUIRE(children.size() == 12);
    for(std::size_t i = 0; i < 12; ++i)
    {
        const std::string expected_path = "cluster/node[" + std::to_string(i) + "]";
        REQUIRE(children[i].path() == expected_path);
    }

    // visit() must also yield numeric order.
    std::vector<std::string> visited_instances;
    cfg.root()["cluster"]["node"].visit([&](const nucleus::config_node &n) {
        if(n.kind() == nucleus::node_kind::container)
            visited_instances.push_back(std::string(n.path()));
        return true;
    });
    REQUIRE(visited_instances.size() == 12);
    for(std::size_t i = 0; i < 12; ++i)
    {
        const std::string expected = "cluster/node[" + std::to_string(i) + "]";
        REQUIRE(visited_instances[i] == expected);
    }
}

// ---------------------------------------------------------------------------
// walk() -- enter/leave walker
// ---------------------------------------------------------------------------

TEST_CASE("config_tree_walker enter called on descent, leave on ascent",
          "[config_node][walker]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");

    recording_walker walker;
    cfg.root()["cluster"]["node"].walk(walker);

    // enter should precede leave for each node.
    REQUIRE_FALSE(walker.events.empty());

    // First event should be enter for "cluster/node".
    REQUIRE(walker.events.front().enter);
    REQUIRE(walker.events.front().path == "cluster/node");

    // The last event should be a leave.
    REQUIRE_FALSE(walker.events.back().enter);

    // Every enter must have a matching leave in balanced order.
    int depth = 0;
    for(const auto &ev : walker.events)
    {
        if(ev.enter)
            ++depth;
        else
        {
            REQUIRE(depth > 0);
            --depth;
        }
    }
    REQUIRE(depth == 0);
}

TEST_CASE("config_tree_walker enter returning false skips children",
          "[config_node][walker]")
{
    const nucleus::config cfg = load_two_nodes("80", "8080");

    no_recurse_walker walker;
    cfg.root()["cluster"]["node"].walk(walker);

    // Should see "cluster/node" entered, but NOT "cluster/node[0]/port" etc.
    // (The no_recurse_walker stops recursion at nodes with '[' in path.)
    REQUIRE_FALSE(walker.entered.empty());
    REQUIRE(walker.entered[0] == "cluster/node");

    // No deep entries like "cluster/node[0]/port".
    for(const auto &path : walker.entered)
        REQUIRE(path.find("port") == std::string::npos);
}

// ---------------------------------------------------------------------------
// deep navigation: cfg.root()["cluster"]["node"][0]["endpoint"]["port"]
// ---------------------------------------------------------------------------

TEST_CASE("config_node deep navigation reaches scalar value", "[config_node][navigation]")
{
    nucleus::config_space_builder engine;
    declare_cluster_schema(engine);
    nucleus::config_space space = nucleus::builder_result_test::built(engine);

    const std::string xml =
        "<cluster>"
        "  <node><endpoint><port>9090</port></endpoint></node>"
        "  <node><endpoint><port>9091</port></endpoint></node>"
        "</cluster>";
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{xml_of(xml)}, {});
    REQUIRE(loaded);
    const nucleus::config &cfg = loaded.value();

    auto port = cfg.root()["cluster"]["node"][0]["endpoint"]["port"];
    REQUIRE(port.exists());
    REQUIRE(port.kind() == nucleus::node_kind::scalar);
    auto v = port.as<std::string>();
    REQUIRE(v);
    REQUIRE(*v == "9090");
}

// ---------------------------------------------------------------------------
// large-tree navigation exercises the ordered-map lower_bound range scans:
// a deep container chain, a repeated container with >= 12 ordinals (so [10]/[11]
// are reached), a container mixing a scalar and a repeated child, and plain
// leaves surrounding the ranges. Built directly from an ordered value map so the
// scans see many neighboring keys, not just the handful a small config yields.
// ---------------------------------------------------------------------------

namespace {

nucleus::config make_large_tree()
{
    std::map<std::string, std::string> values;
    values.emplace("alpha", "1");
    values.emplace("cluster/name", "c1");
    for(int i = 0; i < 12; ++i)
        values.emplace("cluster/node[" + std::to_string(i) + "]/port",
                       std::to_string(i * 10));
    values.emplace("deep/a/b/c/d/leaf", "x");
    values.emplace("deep/a/b/sibling", "y");
    values.emplace("zed", "9");
    auto made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    return std::move(made).value();
}

std::vector<std::string> child_paths(const std::vector<nucleus::config_node> &nodes)
{
    std::vector<std::string> out;
    out.reserve(nodes.size());
    for(const auto &n : nodes)
        out.push_back(std::string(n.path()));
    return out;
}

}

TEST_CASE("config_node large-tree navigation matches shapes via lower_bound scans",
          "[config_node][navigation][range_scan]")
{
    const nucleus::config cfg = make_large_tree();

    REQUIRE(cfg.root().exists());
    REQUIRE(cfg.root().kind() == nucleus::node_kind::container);
    REQUIRE(child_paths(cfg.root().children())
            == std::vector<std::string>{"alpha", "cluster", "deep", "zed"});

    // Plain leaves surrounding the ranges are scalars with no children.
    REQUIRE(cfg.root()["alpha"].kind() == nucleus::node_kind::scalar);
    REQUIRE(cfg.root()["alpha"].count() == 1);
    REQUIRE(cfg.root()["alpha"].children().empty());
    REQUIRE(cfg.root()["zed"].kind() == nucleus::node_kind::scalar);
    REQUIRE_FALSE(cfg.root()["missing"].exists());

    // Container mixing a scalar child and a repeated child.
    auto cluster = cfg.root()["cluster"];
    REQUIRE(cluster.kind() == nucleus::node_kind::container);
    REQUIRE(child_paths(cluster.children())
            == std::vector<std::string>{"cluster/name", "cluster/node"});
    REQUIRE(cluster["name"].kind() == nucleus::node_kind::scalar);
    REQUIRE(*cluster["name"].value() == "c1");

    // Repeated container: 12 instances, ordinals 0..11 in numeric order, [10]/[11]
    // reachable via operator[] and out-of-range null-view above the span.
    auto node = cluster["node"];
    REQUIRE(node.kind() == nucleus::node_kind::repeated);
    REQUIRE(node.count() == 12);
    const auto instances = node.children();
    REQUIRE(instances.size() == 12);
    for(std::size_t i = 0; i < 12; ++i)
        REQUIRE(instances[i].path() == "cluster/node[" + std::to_string(i) + "]");

    REQUIRE(node[10].exists());
    REQUIRE(node[10].path() == "cluster/node[10]");
    REQUIRE(node[10].kind() == nucleus::node_kind::container);
    REQUIRE(node[11].exists());
    REQUIRE_FALSE(node[12].exists());
    REQUIRE_FALSE(node[99].exists());
    REQUIRE(child_paths(node[10].children())
            == std::vector<std::string>{"cluster/node[10]/port"});
    REQUIRE(*node[10]["port"].value() == "100");

    // Deep container chain: an intermediate container with two children, and a
    // scalar reached through six navigation hops.
    auto b = cfg.root()["deep"]["a"]["b"];
    REQUIRE(b.kind() == nucleus::node_kind::container);
    REQUIRE(child_paths(b.children())
            == std::vector<std::string>{"deep/a/b/c", "deep/a/b/sibling"});
    auto leaf = cfg.root()["deep"]["a"]["b"]["c"]["d"]["leaf"];
    REQUIRE(leaf.exists());
    REQUIRE(leaf.kind() == nucleus::node_kind::scalar);
    REQUIRE(*leaf.value() == "x");
}

// ---------------------------------------------------------------------------
// integration: visit() pre-order and walker enter/leave over loaded config
// ---------------------------------------------------------------------------

TEST_CASE("config_node visit -- depth-first pre-order over loaded config",
          "[config_node][visit]")
{
    // Two-node cluster config loaded via load_config.
    const nucleus::config cfg = load_two_nodes("80", "443");

    std::vector<std::string> visited;
    cfg.root().visit([&](const nucleus::config_node &n) {
        visited.push_back(std::string(n.path()));
        return true;
    });

    // Pre-order: root first, then cluster, then cluster/node (repeated),
    // then node[0] and its children, then node[1] and its children.
    REQUIRE_FALSE(visited.empty());
    REQUIRE(visited[0].empty()); // root has empty path

    // node[0] must appear in the visited list before node[1].
    auto pos0 = std::find(visited.begin(), visited.end(), "cluster/node[0]");
    auto pos1 = std::find(visited.begin(), visited.end(), "cluster/node[1]");
    REQUIRE(pos0 != visited.end());
    REQUIRE(pos1 != visited.end());
    REQUIRE(std::distance(pos0, pos1) > 0); // node[0] comes before node[1]

    // node[0]/port must be visited before node[1].
    auto pos0_port = std::find(visited.begin(), visited.end(), "cluster/node[0]/port");
    REQUIRE(pos0_port != visited.end());
    // node[0]/port must precede node[1] (entire node[0] subtree before node[1]).
    REQUIRE(std::distance(pos0_port, pos1) > 0);
}

TEST_CASE("config_node visit -- returning false stops the walk over loaded config",
          "[config_node][visit]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    std::size_t count = 0;
    cfg.root()["cluster"]["node"].visit([&](const nucleus::config_node &) {
        ++count;
        return false; // stop after first node visited
    });

    REQUIRE(count == 1);
}

TEST_CASE("config_node walker -- enter/leave order over loaded config",
          "[config_node][walker]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    recording_walker walker;
    cfg.root()["cluster"]["node"].walk(walker);

    // Each enter must have a corresponding leave (balanced depth counter).
    REQUIRE_FALSE(walker.events.empty());
    int depth = 0;
    for(const auto &ev : walker.events)
    {
        if(ev.enter)
            ++depth;
        else
        {
            REQUIRE(depth > 0);
            --depth;
        }
    }
    REQUIRE(depth == 0); // every enter matched by a leave

    // Enter for each node must precede its leave (LIFO nesting).
    // For every enter event, the corresponding leave must come after.
    std::map<std::string, std::size_t> enter_idx;
    for(std::size_t i = 0; i < walker.events.size(); ++i)
    {
        const auto &ev = walker.events[i];
        if(ev.enter)
        {
            enter_idx[ev.path] = i;
        }
        else
        {
            auto it = enter_idx.find(ev.path);
            REQUIRE(it != enter_idx.end());
            REQUIRE(it->second < i); // enter preceded the leave
        }
    }
}

// ---------------------------------------------------------------------------
// operator[](size_t) returns a true null-view for out-of-range indices
// ---------------------------------------------------------------------------

TEST_CASE("config_node operator[](size_t) out-of-range returns null-view",
          "[config_node][null_view][WR05]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    // node has ordinals 0 and 1 only; ordinal 2 is out of range.
    auto node = cfg.root()["cluster"]["node"];
    REQUIRE(node.kind() == nucleus::node_kind::repeated);

    auto out_of_range = node[2];
    // Must be a true null-view: exists() == false.
    REQUIRE_FALSE(out_of_range.exists());

    // Further navigation from a null-view stays null-view.
    auto further = out_of_range["port"];
    REQUIRE_FALSE(further.exists());
    auto deep = out_of_range[0];
    REQUIRE_FALSE(deep.exists());

    // In-range ordinals still work.
    REQUIRE(node[0].exists());
    REQUIRE(node[1].exists());
}

// ---------------------------------------------------------------------------
// config_node::parent() / ancestor()
// ---------------------------------------------------------------------------

TEST_CASE("config_node parent -- null and root nodes return null",
          "[config_node][parent][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    // Null-view node returns null.
    nucleus::config_node null_node;
    REQUIRE_FALSE(null_node.parent().exists());

    // Root (empty-path) node returns null.
    REQUIRE_FALSE(cfg.root().parent().exists());
}

TEST_CASE("config_node parent -- single-segment path returns root",
          "[config_node][parent][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    auto cluster = cfg.root()["cluster"];
    REQUIRE(cluster.exists());

    auto p = cluster.parent();
    // Parent of "cluster" is the root (empty path).
    REQUIRE(p.exists());
    REQUIRE(p.path().empty());
}

TEST_CASE("config_node parent -- multi-segment path strips last segment",
          "[config_node][parent][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    auto port = cfg.root()["cluster"]["node"][0]["port"];
    REQUIRE(port.exists());
    REQUIRE(port.path() == "cluster/node[0]/port");

    // parent of "cluster/node[0]/port" is "cluster/node[0]"
    auto p1 = port.parent();
    REQUIRE(p1.path() == "cluster/node[0]");

    // parent of "cluster/node[0]" is "cluster"
    auto p2 = p1.parent();
    REQUIRE(p2.path() == "cluster");

    // parent of "cluster" is root
    auto p3 = p2.parent();
    REQUIRE(p3.path().empty());

    // parent of root is null
    REQUIRE_FALSE(p3.parent().exists());
}

TEST_CASE("config_node parent -- indexed segment: parent of node[0] is cluster",
          "[config_node][parent][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    auto node0 = cfg.root()["cluster"]["node"][0];
    REQUIRE(node0.exists());
    REQUIRE(node0.path() == "cluster/node[0]");

    auto p = node0.parent();
    REQUIRE(p.path() == "cluster");
}

TEST_CASE("config_node ancestor -- finds first ancestor matching base name",
          "[config_node][ancestor][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    auto port = cfg.root()["cluster"]["node"][0]["port"];
    REQUIRE(port.exists());

    // ancestor("cluster") walks up from "cluster/node[0]/port" and finds "cluster".
    auto anc = port.ancestor("cluster");
    REQUIRE(anc.exists());
    REQUIRE(anc.path() == "cluster");
}

TEST_CASE("config_node ancestor -- matches indexed segment base name (strips ordinal)",
          "[config_node][ancestor][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    auto port = cfg.root()["cluster"]["node"][0]["port"];
    REQUIRE(port.exists());

    // "node[0]" has base name "node" -- ancestor("node") should match it.
    auto anc = port.ancestor("node");
    REQUIRE(anc.exists());
    REQUIRE(anc.path() == "cluster/node[0]");
}

TEST_CASE("config_node ancestor -- returns null when no ancestor matches",
          "[config_node][ancestor][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    auto port = cfg.root()["cluster"]["node"][0]["port"];
    REQUIRE(port.exists());

    // No ancestor named "server" exists.
    REQUIRE_FALSE(port.ancestor("server").exists());
}

TEST_CASE("config_node ancestor -- returns null on null and root nodes",
          "[config_node][ancestor][REF08]")
{
    const nucleus::config cfg = load_two_nodes("80", "443");

    nucleus::config_node null_node;
    REQUIRE_FALSE(null_node.ancestor("cluster").exists());

    // Root has no ancestors.
    REQUIRE_FALSE(cfg.root().ancestor("cluster").exists());
}

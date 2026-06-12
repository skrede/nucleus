// config_node cursor: null-view chaining navigation, shape queries, visit(), walk()
// D-13: value-semantic cursor entered via config::root()
// D-14: navigation never fails loudly; as<T>() returns expected with full path
// D-15: kind(), count(), children(), exists(), path()
// D-16: pre-order visit() + enter/leave config_tree_walker
// D-07: repeated instances in numeric ordinal order (correct for N >= 11)

#include "nucleus/config_node.h"
#include "nucleus/config_space.h"

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
    nucleus::config_space space = engine.build();

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

} // namespace

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
// N >= 11 numeric ordinal order (D-07) -- must NOT be lexicographic
// ---------------------------------------------------------------------------

TEST_CASE("config_node N >= 11 instances visit in numeric order not lexicographic",
          "[config_node][visit][ordinal][n_large]")
{
    // Build 12 instances. Lexicographic order of "node[N]" would be:
    //   0, 1, 10, 11, 2, 3, 4, 5, 6, 7, 8, 9  (WRONG)
    // Numeric order is: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11  (CORRECT)
    nucleus::config_space_builder engine;
    declare_flat_cluster_schema(engine);
    nucleus::config_space space = engine.build();

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
    nucleus::config_space space = engine.build();

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
// D-16 integration: visit() pre-order and walker enter/leave over loaded config
// ---------------------------------------------------------------------------

TEST_CASE("config_node visit -- depth-first pre-order over loaded config",
          "[config_node][visit][D16]")
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
          "[config_node][visit][D16]")
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
          "[config_node][walker][D16]")
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

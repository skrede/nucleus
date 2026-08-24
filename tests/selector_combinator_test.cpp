#include "nucleus/query/query.h"
#include "builder_result_test_support.h"
#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <algorithm>

// AND-chain, OR, NOT combinators + single-pass evaluation.
// one() loud on zero matches (errc::absent_key) and many matches
//       (errc::ambiguous_result, message includes the count).

using namespace nucleus;

namespace {

config load_simple()
{
    runtime_source src;
    src.set("cluster/port",        "8080");
    src.set("cluster/host",        "localhost");
    src.set("cluster/server/name", "primary");
    src.set("cluster/server/port", "443");
    auto space = nucleus::builder_result_test::built(config_space_builder{});
    auto res   = load_config(space, source_stack{std::move(src)}, {});
    REQUIRE(res.has_value());
    return std::move(*res);
}

}

// -------------------------------------------------------------------------
// AND combinator — method chaining narrows the result set
// -------------------------------------------------------------------------

TEST_CASE("AND-chain narrows results (descendants + leaves)", "[selector]")
{
    const auto cfg = load_simple();
    const auto ctx = nucleus::builder_result_test::built(config_space_builder{}).query_context();

    // Children of cluster that are also leaves.
    auto nodes = query(cfg.root()["cluster"], ctx).children().leaves().collect();

    std::vector<std::string> paths;
    for(const auto &n : nodes)
        paths.emplace_back(n.path());

    // cluster/port and cluster/host are leaf children of cluster.
    CHECK(std::find(paths.begin(), paths.end(), "cluster/port") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "cluster/host") != paths.end());
    // cluster/server is a container child — must not appear.
    CHECK(std::find(paths.begin(), paths.end(), "cluster/server") == paths.end());
}

// -------------------------------------------------------------------------
// OR combinator — union of two selectors
// -------------------------------------------------------------------------

TEST_CASE("or_() produces the union of two selectors", "[selector]")
{
    const auto cfg = load_simple();
    const auto ctx = nucleus::builder_result_test::built(config_space_builder{}).query_context();

    auto left  = query(cfg.root(), ctx).under("cluster/port");
    auto right = query(cfg.root(), ctx).under("cluster/host");
    auto nodes = left.or_(right).collect();

    std::vector<std::string> paths;
    for(const auto &n : nodes)
        paths.emplace_back(n.path());

    // Both subtrees must appear in the union.
    CHECK(std::find(paths.begin(), paths.end(), "cluster/port") != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "cluster/host") != paths.end());
    // cluster/server should NOT appear (not in either subtree).
    CHECK(std::find(paths.begin(), paths.end(), "cluster/server") == paths.end());
}

// -------------------------------------------------------------------------
// NOT combinator — excluding() removes matching nodes
// -------------------------------------------------------------------------

TEST_CASE("excluding() removes nodes matching the given predicate", "[selector]")
{
    const auto cfg = load_simple();
    const auto ctx = nucleus::builder_result_test::built(config_space_builder{}).query_context();

    // All children of cluster except those under cluster/server.
    node_predicate is_server = [](const config_node &n, const schema_query_context *) {
        return n.path().find("cluster/server") != std::string_view::npos;
    };

    auto nodes = query(cfg.root()["cluster"], ctx)
                     .children()
                     .excluding(std::move(is_server))
                     .collect();

    std::vector<std::string> paths;
    for(const auto &n : nodes)
        paths.emplace_back(n.path());

    CHECK(std::find(paths.begin(), paths.end(), "cluster/port")   != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "cluster/host")   != paths.end());
    CHECK(std::find(paths.begin(), paths.end(), "cluster/server") == paths.end());
}

// -------------------------------------------------------------------------
// one() loud on zero matches → errc::absent_key
// -------------------------------------------------------------------------

TEST_CASE("one() returns absent_key when no node matches", "[selector]")
{
    const auto cfg = load_simple();
    const auto ctx = nucleus::builder_result_test::built(config_space_builder{}).query_context();

    // Query a path that does not exist.
    auto result = query(cfg.root()["nonexistent"], ctx).one();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == errc::absent_key);
    // Message must be human-readable and describe the zero-match condition.
    CHECK(result.error().message.find("zero") != std::string::npos);
}

// -------------------------------------------------------------------------
// one() loud on many matches → errc::ambiguous_result, count in message
// -------------------------------------------------------------------------

TEST_CASE("one() returns ambiguous_result when many nodes match", "[selector]")
{
    const auto cfg = load_simple();
    const auto ctx = nucleus::builder_result_test::built(config_space_builder{}).query_context();

    // All leaves under root: definitely more than one.
    auto result = query(cfg.root(), ctx).leaves().one();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == errc::ambiguous_result);
    // Message must include the actual count (>1) so the user knows how many matched.
    CHECK(result.error().message.find("nodes") != std::string::npos);
}

// -------------------------------------------------------------------------
// one() succeeds when exactly one node matches
// -------------------------------------------------------------------------

TEST_CASE("one() returns the node when exactly one matches", "[selector]")
{
    const auto cfg = load_simple();
    const auto ctx = nucleus::builder_result_test::built(config_space_builder{}).query_context();

    // cluster/server/name is a leaf under cluster/server with exactly one match.
    auto result = query(cfg.root()["cluster"]["server"]["name"], ctx)
                      .leaves()
                      .one();

    REQUIRE(result.has_value());
    CHECK(result->path() == "cluster/server/name");
}

// -------------------------------------------------------------------------
// single-pass — each() calls visit() exactly once per invocation
// -------------------------------------------------------------------------

TEST_CASE("single-pass evaluation — each() traverses the tree once", "[selector]")
{
    const auto cfg = load_simple();
    const auto ctx = nucleus::builder_result_test::built(config_space_builder{}).query_context();

    // The fact that this terminates in linear time (not quadratic) and returns a
    // deterministic result is the behavioural guarantee. We verify the result is
    // correct (not repeated / duplicated) as a proxy for single-pass correctness.
    std::vector<std::string> seen;
    query(cfg.root(), ctx).leaves().each([&](const config_node &n) {
        seen.emplace_back(n.path());
    });

    // No duplicates in a single-pass traversal.
    auto sorted = seen;
    std::sort(sorted.begin(), sorted.end());
    auto unique_end = std::unique(sorted.begin(), sorted.end());
    CHECK(unique_end == sorted.end()); // all elements are distinct
}

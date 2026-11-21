int query_example_main();
#define main query_example_main
#include "../../examples/references/query.cpp"
#undef main

#include "nucleus/error.h"
#include "nucleus/expected.h"
#include "nucleus/identity.h"
#include "nucleus/config_node.h"
#include "nucleus/config_space.h"

#include "nucleus/config_source/source_stack.h"

#include "nucleus/query/query.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <sstream>
#include <utility>
#include <algorithm>
#include <string_view>

namespace {

nucleus::error expected_ambiguous_error()
{
    return {nucleus::errc::ambiguous_result,
            "query matched 2 nodes; one() requires exactly one match"};
}

struct query_report
{
    int         status;
    std::string output;
    std::string errors;
};

query_report report_ambiguous(nucleus::expected<nucleus::config_node, nucleus::error> result)
{
    std::ostringstream output;
    std::ostringstream errors;
    const int          status = show_ambiguous_one(output, errors, std::move(result));
    return {status, output.str(), errors.str()};
}

query_report report_single(nucleus::expected<nucleus::config_node, nucleus::error> result)
{
    std::ostringstream output;
    std::ostringstream errors;
    const int          status = show_single_one(output, errors, std::move(result));
    return {status, output.str(), errors.str()};
}

}

TEST_CASE("ambiguous query reporting requires the exact error", "[selector][example]")
{
    const auto success = report_ambiguous(nucleus::config_node{});
    REQUIRE(success.status == 1);
    const auto wrong_code = report_ambiguous(nucleus::unexpected(nucleus::error{
            nucleus::errc::absent_key,
            "query matched 2 nodes; one() requires exactly one match"}));
    REQUIRE(wrong_code.status == 1);
    const auto wrong_message = report_ambiguous(nucleus::unexpected(nucleus::error{
            nucleus::errc::ambiguous_result, "query matched many nodes"}));
    REQUIRE(wrong_message.status == 1);
    const auto expected = report_ambiguous(nucleus::unexpected(expected_ambiguous_error()));
    REQUIRE(expected.status == 0);
    REQUIRE(expected.output.find(expected_ambiguous_error().message) != std::string::npos);
    REQUIRE(expected.errors.empty());
}

TEST_CASE("single query reporting requires the exact primary value", "[selector][example]")
{
    auto space = make_server_space();
    REQUIRE(space.has_value());
    auto loaded = load_config(*space, nucleus::source_stack{make_server_source()}, {});
    REQUIRE(loaded.has_value());
    const auto error = report_single(nucleus::unexpected(nucleus::error{
            nucleus::errc::absent_key, "injected single failure"}));
    REQUIRE(error.status == 1);
    REQUIRE(report_single(nucleus::config_node{}).status == 1);
    REQUIRE(report_single(loaded->root()["cluster"]["server"][std::size_t{1}]["name"]).status == 1);
    const auto primary =
            report_single(loaded->root()["cluster"]["server"][std::size_t{0}]["name"]);
    REQUIRE(primary.status == 0);
    REQUIRE(primary.output.find("  name: primary\n") != std::string::npos);
    REQUIRE(primary.errors.empty());
}

TEST_CASE("query result mismatches stop later query sections", "[selector][example]")
{
    auto space = make_server_space();
    REQUIRE(space.has_value());
    auto loaded = load_config(*space, nucleus::source_stack{make_server_source()}, {});
    REQUIRE(loaded.has_value());
    const auto         ctx     = space->query_context();
    const auto         primary = loaded->root()["cluster"]["server"][std::size_t{0}]["name"];
    std::ostringstream output;
    std::ostringstream errors;
    REQUIRE(run_queries(*loaded, ctx, nucleus::config_node{}, primary, output, errors) == 1);
    REQUIRE(output.str().find("one() on server[0]") == std::string::npos);
    output.str({});
    errors.str({});
    REQUIRE(run_queries(*loaded, ctx, nucleus::unexpected(expected_ambiguous_error()),
                        nucleus::config_node{}, output, errors) == 1);
    REQUIRE(output.str().find("children().leaves()") == std::string::npos);
}

TEST_CASE("network ownership includes every concrete host leaf", "[selector][example]")
{
    auto space = make_server_space();
    REQUIRE(space.has_value());
    auto loaded = load_config(*space, nucleus::source_stack{make_server_source()}, {});
    REQUIRE(loaded.has_value());
    const auto                    nodes = query(loaded->root(), space->query_context()).owned_by(network_owner).collect();
    std::vector<std::string_view> paths;
    for(const auto &node : nodes)
        paths.push_back(node.path());
    REQUIRE(nodes.size() == 10);
    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/server[0]/host") != paths.end());
    REQUIRE(std::find(paths.begin(), paths.end(), "cluster/server[1]/host") != paths.end());
}

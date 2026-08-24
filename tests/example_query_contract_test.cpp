int query_example_main();
#define main query_example_main
#include "../examples/references/query.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <sstream>
#include <utility>
#include <algorithm>
#include <string_view>

namespace {

nucleus::error query_setup_error(std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected query setup failure " + std::to_string(ordinal)};
}

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

class scripted_query_builder
{
public:
    explicit scripted_query_builder(std::size_t fail_at)
            : m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element,
                                                  const nucleus::owner_token & = {})
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(query_setup_error(m_fail_at));
        return nucleus::registration_ok();
    }

    nucleus::expected<nucleus::config_space, nucleus::error> build()
    {
        ++m_build_count;
        return nucleus::config_space_builder{}.build();
    }

    std::size_t call_count() const { return m_call_count; }

    std::size_t build_count() const { return m_build_count; }

private:
    std::size_t m_fail_at;
    std::size_t m_call_count;
    std::size_t m_build_count;
};

}

TEST_CASE("query setup preserves each first registration failure", "[selector][example]")
{
    for(std::size_t ordinal = 1; ordinal <= 5; ++ordinal)
    {
        DYNAMIC_SECTION("registration " << ordinal)
        {
            scripted_query_builder builder(ordinal);
            const auto             result = define_server_space(builder);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == query_setup_error(ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

TEST_CASE("query product and terminal preserve setup failure", "[selector][example]")
{
    scripted_query_builder builder(3);
    auto                   product = make_server_space(builder);
    REQUIRE_FALSE(product.has_value());
    REQUIRE(product.error() == query_setup_error(3));
    REQUIRE(builder.call_count() == 3);
    REQUIRE(builder.build_count() == 0);
    std::ostringstream output;
    std::ostringstream errors;
    const int          status = run_query_example(std::move(product), output, errors);
    REQUIRE(status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() ==
            "space setup failed: rejected_registration: injected query setup failure 3\n");
}

TEST_CASE("query setup retains the real builder path", "[selector][example]")
{
    auto product = make_server_space();
    REQUIRE(product.has_value());
    REQUIRE(product->schema_elements().size() == 5);
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

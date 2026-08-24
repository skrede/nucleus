#define main constraint_groups_example_main
#include "../examples/schema/constraint_groups.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <cstddef>
#include <sstream>
#include <utility>
#include <string_view>

namespace {

nucleus::error constraint_setup_error(std::string_view context, std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected " + std::string(context) + " setup failure " + std::to_string(ordinal)};
}

class scripted_constraint_builder
{
public:
    scripted_constraint_builder(std::string context, std::size_t fail_at)
            : m_context(std::move(context))
            , m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element) { return next_result(); }

    nucleus::registration_result register_constraint_group(nucleus::constraint_group) { return next_result(); }

    nucleus::registration_result register_identity_group(nucleus::identity_group_spec) { return next_result(); }

    nucleus::expected<nucleus::config_space, nucleus::error> build()
    {
        ++m_build_count;
        return nucleus::config_space_builder{}.build();
    }

    std::size_t call_count() const { return m_call_count; }

    std::size_t build_count() const { return m_build_count; }

private:
    nucleus::registration_result next_result()
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(constraint_setup_error(m_context, m_fail_at));
        return nucleus::registration_ok();
    }

    std::string m_context;
    std::size_t m_fail_at;
    std::size_t m_call_count;
    std::size_t m_build_count;
};

template<typename Define>
void verify_constraint_positions(std::string_view context, std::size_t count, Define define)
{
    for(std::size_t ordinal = 1; ordinal <= count; ++ordinal)
    {
        scripted_constraint_builder builder(std::string(context), ordinal);
        const auto                  result = define(builder);
        CAPTURE(context, ordinal);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == constraint_setup_error(context, ordinal));
        REQUIRE(builder.call_count() == ordinal);
        REQUIRE(builder.build_count() == 0);
    }
}

load_result load_ttl(const config_space &space, std::string_view ttl)
{
    runtime_source source;
    source.set("server/cache/ttl", std::string(ttl)).set("server/auth/token", "t");
    return load_config(space, source_stack{std::move(source)}, {});
}

void require_mismatch(load_result result, expected_outcome expected,
                      std::string_view actual)
{
    std::ostringstream output;
    std::ostringstream errors;
    REQUIRE(report_result(std::move(result), expected, output, errors) == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str().find("expectation mismatch") != std::string::npos);
    REQUIRE(errors.str().find(actual) != std::string::npos);
}

}

TEST_CASE("constraint setup preserves every first failure", "[constraint][example]")
{
    verify_constraint_positions("cache elements", 4, register_cache_elements<scripted_constraint_builder>);
    verify_constraint_positions("cache groups", 2, register_cache_groups<scripted_constraint_builder>);
    verify_constraint_positions("auth", 5, register_auth_constraints<scripted_constraint_builder>);
    verify_constraint_positions("pool identity", 6, register_pool_identity<scripted_constraint_builder>);
    for(const std::size_t ordinal : {3U, 5U})
    {
        scripted_constraint_builder builder("cache composite", ordinal);
        const auto                  result = register_cache_constraints(builder);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == constraint_setup_error("cache composite", ordinal));
        REQUIRE(builder.call_count() == ordinal);
        REQUIRE(builder.build_count() == 0);
    }
}

TEST_CASE("constraint product preserves setup failure", "[constraint][example]")
{
    for(const std::size_t ordinal : {1U, 9U})
    {
        scripted_constraint_builder builder("space", ordinal);
        const auto                  product = make_space(builder);
        REQUIRE_FALSE(product.has_value());
        REQUIRE(product.error() == constraint_setup_error("space", ordinal));
        REQUIRE(builder.call_count() == ordinal);
        REQUIRE(builder.build_count() == 0);
    }
    std::ostringstream output;
    std::ostringstream errors;
    REQUIRE(run_constraint_groups(nucleus::unexpected(constraint_setup_error("space", 9)), output, errors) == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str().find("injected space setup failure 9") != std::string::npos);
    const auto product = make_space();
    REQUIRE(product.has_value());
    REQUIRE(product->schema_elements().size() == 14);
}

TEST_CASE("constraint ttl accepts only complete positive int32 values", "[constraint][example][ttl]")
{
    const auto product = make_space();
    REQUIRE(product.has_value());
    const auto cases = std::array{
            std::pair{"1", ""}, std::pair{"2147483647", ""},
            std::pair{"0", "ttl must be greater than zero"}, std::pair{"00", "ttl must be greater than zero"}, std::pair{"-1", "ttl must be greater than zero"},
            std::pair{"0.0", "ttl must be a base-10 int32 without trailing characters"},
            std::pair{"garbage", "ttl must be a base-10 int32 without trailing characters"},
            std::pair{" 1", "ttl must be a base-10 int32 without trailing characters"}, std::pair{"1 ", "ttl must be a base-10 int32 without trailing characters"},
            std::pair{"2147483648", "ttl must be a base-10 int32 without trailing characters"},
            std::pair{"-2147483649", "ttl must be a base-10 int32 without trailing characters"}};
    for(const auto &[ttl, cue] : cases)
    {
        const auto result = load_ttl(*product, ttl);
        CAPTURE(ttl);
        if(std::string_view(cue).empty())
            REQUIRE(result.has_value());
        else
        {
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error().code == errc::schema_violation);
            REQUIRE(result.error().message.find(cue) != std::string::npos);
        }
    }
}

TEST_CASE("constraint result comparison rejects every mismatch", "[constraint][example][outcome]")
{
    const auto product = make_space();
    REQUIRE(product.has_value());
    require_mismatch(load_config(*product, source_stack{make_source({{"server/cache/lru", "on"}, {"server/auth/token", "t"}})}, {}),
                     {false, errc::schema_violation, "expected cue"}, "actual: success");
    require_mismatch(nucleus::unexpected(error{errc::schema_violation, "actual rejection"}),
                     {true, errc::schema_violation, {}},
                     "actual error: schema_violation: actual rejection");
    require_mismatch(nucleus::unexpected(error{errc::unmet_capability, "expected cue"}),
                     {false, errc::schema_violation, "expected cue"}, "unmet_capability");
    require_mismatch(nucleus::unexpected(error{errc::schema_violation, "wrong cue"}),
                     {false, errc::schema_violation, "expected cue"}, "wrong cue");
}

TEST_CASE("constraint scenarios declare and enforce exact outcomes", "[constraint][example][outcome]")
{
    const auto product = make_space();
    REQUIRE(product.has_value());
    auto scenarios = make_scenarios();
    REQUIRE(scenarios[0].expected.success);
    const auto cues = std::array{"requires at most 1 active member(s) but 2 are active", "is partially present (1 of 2)", "ttl must be greater than zero", "is not unique within the slice"};
    for(std::size_t index = 1; index < scenarios.size(); ++index)
    {
        REQUIRE_FALSE(scenarios[index].expected.success);
        REQUIRE(scenarios[index].expected.code == errc::schema_violation);
        REQUIRE(scenarios[index].expected.cue == cues[index - 1]);
    }
    std::ostringstream output, errors;
    REQUIRE(run_scenarios(*product, scenarios, output, errors) == 0);
    auto               early_stop = make_scenarios();
    std::ostringstream early_output, early_errors;
    early_stop[0].expected = {false, errc::schema_violation, "wrong declaration"};
    REQUIRE(run_scenarios(*product, early_stop, early_output, early_errors) == 1);
    REQUIRE(early_output.str().find(early_stop[0].title) != std::string::npos);
    REQUIRE(early_output.str().find(early_stop[1].title) == std::string::npos);
}

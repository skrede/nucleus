int schema_example_main();
#define main schema_example_main
#include "../../examples/schema/schema.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <cstddef>
#include <sstream>
#include <utility>
#include <optional>
#include <string_view>

namespace {

struct terminal_case
{
    std::string_view              name;
    std::optional<nucleus::error> rejection;
    int                           status;
    std::string_view              output;
    std::string_view              errors;
};

nucleus::error schema_setup_error(std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected schema setup failure " + std::to_string(ordinal)};
}

class scripted_schema_builder
{
public:
    explicit scripted_schema_builder(std::size_t fail_at)
            : m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element, const nucleus::owner_token & = {})
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(schema_setup_error(m_fail_at));
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

TEST_CASE("schema example accepts only the missing-host rejection",
          "[facade][schema][example]")
{
    constexpr std::string_view         cue = "required field 'server/host' is missing";
    const std::array<terminal_case, 4> cases{{
            {"success", std::nullopt, 1, "", "unexpected success\n"},
            {"wrong code",
             nucleus::error{nucleus::errc::unmet_capability, std::string(cue)},
             1,
             "",
             "unexpected rejection: unmet_capability: required field 'server/host' is missing\n"},
            {"wrong message",
             nucleus::error{nucleus::errc::schema_violation,
                            "required field 'server/port' is missing"},
             1,
             "",
             "unexpected rejection: schema_violation: required field 'server/port' is missing\n"},
            {"expected rejection",
             nucleus::error{nucleus::errc::schema_violation, std::string(cue)},
             0,
             "rejected as expected: schema_violation: required field 'server/host' is missing\n",
             ""},
    }};

    for(const terminal_case &test_case : cases)
    {
        DYNAMIC_SECTION(test_case.name)
        {
            nucleus::load_result loaded =
                    test_case.rejection
                    ? nucleus::load_result(nucleus::unexpected(*test_case.rejection))
                    : nucleus::load_result(nucleus::config{});
            std::ostringstream output;
            std::ostringstream errors;

            const int status = report_load_result(std::move(loaded), output, errors);

            REQUIRE(status == test_case.status);
            REQUIRE(output.str() == test_case.output);
            REQUIRE(errors.str() == test_case.errors);
            if(status != 0)
                REQUIRE(output.str().find("rejected as expected") == std::string::npos);
        }
    }
}

TEST_CASE("schema setup preserves each first registration failure", "[schema][example]")
{
    for(std::size_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        DYNAMIC_SECTION("registration " << ordinal)
        {
            scripted_schema_builder builder(ordinal);
            const auto              result = define_space(builder);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == schema_setup_error(ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

TEST_CASE("schema product and terminal preserve setup failure", "[schema][example]")
{
    scripted_schema_builder builder(2);
    auto                    product = make_space(builder);
    REQUIRE_FALSE(product.has_value());
    REQUIRE(product.error() == schema_setup_error(2));
    REQUIRE(builder.call_count() == 2);
    REQUIRE(builder.build_count() == 0);
    std::ostringstream output;
    std::ostringstream errors;
    const int          status = run_schema_example(std::move(product), output, errors);
    REQUIRE(status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() ==
            "space setup failed: rejected_registration: injected schema setup failure 2\n");
}

TEST_CASE("schema setup retains the real builder path", "[schema][example]")
{
    REQUIRE(make_space().has_value());
}

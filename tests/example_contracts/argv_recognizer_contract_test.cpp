int argv_recognizer_example_main();
#define main argv_recognizer_example_main
#include "../../examples/cli/argv_recognizer.cpp"
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

nucleus::error argv_setup_error(std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected argv setup failure " + std::to_string(ordinal)};
}

class scripted_argv_builder
{
public:
    explicit scripted_argv_builder(std::size_t fail_at)
            : m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_schema(std::string, nucleus::owner_token = {})
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(argv_setup_error(m_fail_at));
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

TEST_CASE("argv recognizer example accepts only the unknown-timeout rejection",
          "[recognizer_of][argv][example]")
{
    constexpr std::string_view cue =
            "unknown CLI flag '--server-timeout=30' maps to undeclared key 'server/timeout'";
    const std::array<terminal_case, 4> cases{{
            {"success", std::nullopt, 1, "", "unexpected success: unrecognized flag accepted\n"},
            {"wrong code",
             nucleus::error{nucleus::errc::unmet_capability, std::string(cue)},
             1,
             "",
             "unexpected rejection: unmet_capability: unknown CLI flag '--server-timeout=30' "
             "maps to undeclared key 'server/timeout'\n"},
            {"wrong message",
             nucleus::error{nucleus::errc::schema_violation,
                            "unknown CLI flag '--server-port=30' maps to undeclared key 'server/port'"},
             1,
             "",
             "unexpected rejection: schema_violation: unknown CLI flag '--server-port=30' maps "
             "to undeclared key 'server/port'\n"},
            {"expected rejection",
             nucleus::error{nucleus::errc::schema_violation, std::string(cue)},
             0,
             "\nunrecognized flag rejected: schema_violation: unknown CLI flag "
             "'--server-timeout=30' maps to undeclared key 'server/timeout'\n",
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

            const int status = report_unknown_rejection(std::move(loaded), output, errors);

            REQUIRE(status == test_case.status);
            REQUIRE(output.str() == test_case.output);
            REQUIRE(errors.str() == test_case.errors);
            if(status != 0)
                REQUIRE(output.str().find("unrecognized flag rejected") == std::string::npos);
        }
    }
}

TEST_CASE("argv setup preserves each first registration failure", "[argv][example]")
{
    for(std::size_t ordinal = 1; ordinal <= 2; ++ordinal)
    {
        DYNAMIC_SECTION("registration " << ordinal)
        {
            scripted_argv_builder builder(ordinal);
            const auto            result = define_space(builder);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == argv_setup_error(ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

TEST_CASE("argv product and terminal preserve setup failure", "[argv][example]")
{
    scripted_argv_builder builder(1);
    auto                  product = make_space(builder);
    REQUIRE_FALSE(product.has_value());
    REQUIRE(product.error() == argv_setup_error(1));
    REQUIRE(builder.call_count() == 1);
    REQUIRE(builder.build_count() == 0);
    std::ostringstream output;
    std::ostringstream errors;
    const int          status = run_recognizer_example(std::move(product), output, errors);
    REQUIRE(status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() ==
            "space setup failed: rejected_registration: injected argv setup failure 1\n");
}

TEST_CASE("argv setup retains the real builder path", "[argv][example]")
{
    REQUIRE(make_space().has_value());
}

#define main schema_example_main
#include "../examples/schema/schema.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
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

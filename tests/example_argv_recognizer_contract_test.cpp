#define main argv_recognizer_example_main
#include "../examples/cli/argv_recognizer.cpp"
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

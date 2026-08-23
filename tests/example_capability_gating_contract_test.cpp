#define main capability_gating_example_main
#include "../examples/sources/capability_gating.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <sstream>
#include <utility>
#include <optional>
#include <string_view>

namespace {

struct capability_terminal_case
{
    std::string_view              name;
    std::optional<nucleus::error> rejection;
    int                           status;
    std::string_view              output;
    std::string_view              errors;
};

}

TEST_CASE("capability gating example accepts only the missing-nesting rejection",
          "[auto-gate][example]")
{
    constexpr std::string_view cue =
            "no source can satisfy capability 'nesting' required by 'schema'";
    const std::array<capability_terminal_case, 4> cases{{
            {"success", std::nullopt, 1, "", "unexpected success: flat source accepted\n"},
            {"wrong code",
             nucleus::error{nucleus::errc::schema_violation, std::string(cue)},
             1,
             "",
             "unexpected rejection: schema_violation: no source can satisfy capability "
             "'nesting' required by 'schema'\n"},
            {"wrong message",
             nucleus::error{nucleus::errc::unmet_capability,
                            "no source can satisfy capability 'typed_scalars' required by 'schema'"},
             1,
             "",
             "unexpected rejection: unmet_capability: no source can satisfy capability "
             "'typed_scalars' required by 'schema'\n"},
            {"expected rejection",
             nucleus::error{nucleus::errc::unmet_capability, std::string(cue)},
             0,
             "load auto-gated and refused: unmet_capability: no source can satisfy capability "
             "'nesting' required by 'schema'\n",
             ""},
    }};

    for(const capability_terminal_case &test_case : cases)
    {
        DYNAMIC_SECTION(test_case.name)
        {
            nucleus::load_result loaded =
                    test_case.rejection
                    ? nucleus::load_result(nucleus::unexpected(*test_case.rejection))
                    : nucleus::load_result(nucleus::config{});
            std::ostringstream output;
            std::ostringstream errors;

            const int status = report_capability_rejection(std::move(loaded), output, errors);

            REQUIRE(status == test_case.status);
            REQUIRE(output.str() == test_case.output);
            REQUIRE(errors.str() == test_case.errors);
            if(status != 0)
                REQUIRE(output.str().find("load auto-gated and refused") == std::string::npos);
        }
    }
}

#include "nucleus/runtime/runtime_source.h"

#define main capability_gating_example_main
#include "../examples/sources/capability_gating.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <cstdint>
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

nucleus::load_result load_capability_port(std::string_view port)
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return nucleus::unexpected(nucleus::error{
                nucleus::errc::rejected_registration, "capability schema setup failed"});
    nucleus::runtime_source values;
    values.set("server/primary/name", "primary");
    values.set("server/primary/port", std::string(port));
    nucleus::load_options options;
    options.selection = "primary";
    return nucleus::load_config(builder.build(),
                                nucleus::source_stack{std::move(values)}, options);
}

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

TEST_CASE("capability port covers the complete uint16 domain", "[auto-gate][example]")
{
    auto maximum = load_capability_port("65535");
    REQUIRE(maximum.has_value());
    auto port = maximum->get_as<std::uint16_t>("server/port");
    REQUIRE(port.has_value());
    REQUIRE(*port == std::uint16_t{65535});

    auto overflow = load_capability_port("65536");
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code == nucleus::errc::failed_conversion);
    REQUIRE(overflow.error().message.find("server/port") != std::string::npos);
    REQUIRE(overflow.error().message.find("value out of range for type") != std::string::npos);
}

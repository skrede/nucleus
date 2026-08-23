#include "nucleus/runtime/runtime_source.h"

#define main capability_gating_example_main
#include "../examples/sources/capability_gating.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <cstddef>
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

nucleus::error gating_setup_error(std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected gating setup failure " + std::to_string(ordinal)};
}

class scripted_gating_builder
{
public:
    explicit scripted_gating_builder(std::size_t fail_at)
            : m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element, const nucleus::owner_token & = {})
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(gating_setup_error(m_fail_at));
        return nucleus::registration_ok();
    }

    nucleus::config_space build()
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

TEST_CASE("gating setup preserves each first registration failure", "[gating][example]")
{
    for(std::size_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        DYNAMIC_SECTION("registration " << ordinal)
        {
            scripted_gating_builder builder(ordinal);
            const auto              result = define_space(builder);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == gating_setup_error(ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

TEST_CASE("gating product and terminal preserve setup failure", "[gating][example]")
{
    scripted_gating_builder builder(3);
    auto                    product = make_space(builder);
    REQUIRE_FALSE(product.has_value());
    REQUIRE(product.error() == gating_setup_error(3));
    REQUIRE(builder.call_count() == 3);
    REQUIRE(builder.build_count() == 0);
    std::ostringstream output;
    std::ostringstream errors;
    const int          status = run_gating_example(std::move(product), output, errors);
    REQUIRE(status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() ==
            "space setup failed: rejected_registration: injected gating setup failure 3\n");
}

TEST_CASE("gating setup retains the real builder path", "[gating][example]")
{
    REQUIRE(make_space().has_value());
}

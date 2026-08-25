int typed_example_main();
#define main typed_example_main
#include "../../examples/schema/typed.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <array>
#include <string>
#include <cstddef>
#include <sstream>
#include <string_view>

namespace {

nucleus::error typed_setup_error(std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected typed setup failure " + std::to_string(ordinal)};
}

class scripted_typed_builder
{
public:
    explicit scripted_typed_builder(std::size_t fail_at)
            : m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element, const nucleus::owner_token & = {})
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(typed_setup_error(m_fail_at));
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

TEST_CASE("typed vector converter consumes exactly three components",
          "[typed][example]")
{
    const auto conversion = make_vec3_converter()("1,2,3");
    REQUIRE(conversion.has_value());
    const vec3 *value = std::any_cast<vec3>(&conversion.value());
    REQUIRE(value != nullptr);
    REQUIRE(value->x == 1.0f);
    REQUIRE(value->y == 2.0f);
    REQUIRE(value->z == 3.0f);
}

TEST_CASE("typed vector converter rejects incomplete consumption",
          "[typed][example]")
{
    constexpr std::array<std::string_view, 6> malformed{
            "", "1,2", "1,2,3,4", ",1,2", "1,2,3,", "1,,3"};
    const auto converter = make_vec3_converter();
    for(const std::string_view input : malformed)
    {
        DYNAMIC_SECTION("vector input '" << input << "'")
        {
            REQUIRE_FALSE(converter(input).has_value());
        }
    }

    const auto scalar = nucleus::make_scalar_converter<float>()("1tail");
    REQUIRE_FALSE(scalar.has_value());
    REQUIRE(scalar.error().find("trailing") != std::string::npos);
}

TEST_CASE("typed setup preserves each first registration failure", "[typed][example]")
{
    for(std::size_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        DYNAMIC_SECTION("registration " << ordinal)
        {
            scripted_typed_builder builder(ordinal);
            const auto             result = define_space(builder);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == typed_setup_error(ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

TEST_CASE("typed product and terminal preserve setup failure", "[typed][example]")
{
    scripted_typed_builder builder(2);
    auto                   product = make_space(builder);
    REQUIRE_FALSE(product.has_value());
    REQUIRE(product.error() == typed_setup_error(2));
    REQUIRE(builder.call_count() == 2);
    REQUIRE(builder.build_count() == 0);
    std::ostringstream output;
    std::ostringstream errors;
    const int          status = run_typed_example(std::move(product), output, errors);
    REQUIRE(status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() ==
            "space setup failed: rejected_registration: injected typed setup failure 2\n");
}

TEST_CASE("typed setup retains the real builder path", "[typed][example]")
{
    REQUIRE(make_space().has_value());
}

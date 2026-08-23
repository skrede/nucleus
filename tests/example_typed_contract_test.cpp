#define main typed_example_main
#include "../examples/schema/typed.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <any>
#include <array>
#include <string>
#include <string_view>

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

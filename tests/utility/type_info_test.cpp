#include "nucleus/utility/type_info.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <typeindex>

namespace {

struct vec3 { float x, y, z; };
enum class color { red, green, blue };

}

TEST_CASE("type_info carries identity, size, and traits", "[utility][type_info]")
{
    SECTION("identity matches std::type_index")
    {
        REQUIRE(nucleus::make_type_info<vec3>().id == std::type_index(typeid(vec3)));
    }

    SECTION("identity is stable across cv-qualification and references")
    {
        const auto &bare = nucleus::make_type_info<vec3>();
        REQUIRE(nucleus::make_type_info<const vec3 &>().id == bare.id);
        REQUIRE(nucleus::make_type_info<vec3 &&>().id == bare.id);
    }

    SECTION("distinct types have distinct identity")
    {
        REQUIRE(nucleus::make_type_info<int>().id != nucleus::make_type_info<double>().id);
    }

    SECTION("size and traits")
    {
        REQUIRE(nucleus::make_type_info<double>().size == sizeof(double));
        REQUIRE(nucleus::make_type_info<double>().is_fundamental);
        REQUIRE_FALSE(nucleus::make_type_info<double>().is_enum);
        REQUIRE(nucleus::make_type_info<color>().is_enum);
        REQUIRE_FALSE(nucleus::make_type_info<vec3>().is_fundamental);
    }
}

TEST_CASE("type_info name is the readable toolchain spelling", "[utility][type_info]")
{
    REQUIRE(nucleus::make_type_info<vec3>().name.find("vec3") != std::string_view::npos);
    REQUIRE(nucleus::make_type_info<int>().name == "int");
    REQUIRE(nucleus::make_type_info<color>().name.find("color") != std::string_view::npos);
}

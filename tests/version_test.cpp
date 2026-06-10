#include "nucleus/version.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

// The version is declared in three places: the CMake project(), the version.h
// macros, and the version() string. This test pins all three together so a bump
// that misses one of them fails loudly instead of drifting.

#define NUCLEUS_STRINGIFY_IMPL(x) #x
#define NUCLEUS_STRINGIFY(x) NUCLEUS_STRINGIFY_IMPL(x)

TEST_CASE("version() matches the version.h macros", "[version]")
{
    const std::string from_macros = NUCLEUS_STRINGIFY(NUCLEUS_VERSION_MAJOR) "."
                                    NUCLEUS_STRINGIFY(NUCLEUS_VERSION_MINOR) "."
                                    NUCLEUS_STRINGIFY(NUCLEUS_VERSION_PATCH);
    REQUIRE(std::string(nucleus::version()) == from_macros);
}

TEST_CASE("version() matches the CMake project version", "[version]")
{
    REQUIRE(std::string(nucleus::version()) == NUCLEUS_CMAKE_PROJECT_VERSION);
}

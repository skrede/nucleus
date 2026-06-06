#include "nucleus/format.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("format produces a std::string from a spec and arguments", "[format]")
{
    const std::string message = nucleus::format("{} = {}", "key", 42);
    REQUIRE(message == "key = 42");
}

TEST_CASE("format handles no arguments", "[format]")
{
    REQUIRE(nucleus::format("literal") == "literal");
}

#include "nucleus/version.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version is reported", "[version]")
{
    REQUIRE_FALSE(nucleus::version().empty());
}

#include "nucleus/result.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

nucleus::result<int, std::string> parse(bool ok)
{
    if(ok)
        return 42;
    return nucleus::fail<std::string>("bad input");
}

}

TEST_CASE("result carries a value on success", "[result]")
{
    auto r = parse(true);
    REQUIRE(r.has_value());
    REQUIRE(static_cast<bool>(r));
    REQUIRE(r.value() == 42);
}

TEST_CASE("result carries an error on failure", "[result]")
{
    auto r = parse(false);
    REQUIRE_FALSE(r.has_value());
    REQUIRE_FALSE(static_cast<bool>(r));
    REQUIRE(r.error() == "bad input");
}

TEST_CASE("value_or falls back to a default on error", "[result]")
{
    REQUIRE(parse(true).value_or(0) == 42);
    REQUIRE(parse(false).value_or(7) == 7);
}

TEST_CASE("failure tag disambiguates same-typed value and error", "[result]")
{
    nucleus::result<int, int> ok(5);
    nucleus::result<int, int> err(nucleus::fail(9));
    REQUIRE(ok.has_value());
    REQUIRE(ok.value() == 5);
    REQUIRE_FALSE(err.has_value());
    REQUIRE(err.error() == 9);
}

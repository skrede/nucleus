#include "nucleus/error.h"

#include "nucleus/argv/multispace_argv_source.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace nucleus;

TEST_CASE("multispace_argv_source: for_space with an unregistered name reports malformed_source",
          "[multispace_argv][argv]")
{
    multispace_argv_source src({});
    src.register_space("alpha");
    auto view = src.for_space("delta");
    REQUIRE_FALSE(view);
    REQUIRE(view.error().code == errc::malformed_source);
    REQUIRE(view.error().message.find("delta") != std::string::npos);
}

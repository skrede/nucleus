#include "nucleus/error.h"
#include "nucleus/config_space.h"

#include "support/builder_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

using nucleus::errc;

TEST_CASE("registering on a built builder fails with errc::sealed_builder",
          "[error][code]")
{
    nucleus::config_space_builder builder;
    (void)nucleus::builder_result_test::built(builder);

    auto rejected = builder.register_schema("server/host");
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == errc::sealed_builder);
}

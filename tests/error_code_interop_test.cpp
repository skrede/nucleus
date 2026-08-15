#include "nucleus/error.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <system_error>

using nucleus::errc;

TEST_CASE("destination errors have an exact machine-readable name", "[error][interop]")
{
    CHECK(nucleus::to_string(errc::unwritable_destination) ==
          "unwritable_destination");
}

TEST_CASE("errc interoperates with std::error_code by category identity",
          "[error][interop]")
{
    const std::error_code explicit_code =
            nucleus::make_error_code(errc::unwritable_destination);
    CHECK(explicit_code.value() == static_cast<int>(errc::unwritable_destination));
    CHECK(&explicit_code.category() == &nucleus::errc_category());
    CHECK(std::string(explicit_code.category().name()) == "nucleus");
    CHECK(explicit_code.message() == "unwritable_destination");

    const std::error_code implicit_code = errc::unwritable_destination;
    CHECK(static_cast<bool>(implicit_code));
    CHECK(implicit_code == explicit_code);
    CHECK(implicit_code == errc::unwritable_destination);
}

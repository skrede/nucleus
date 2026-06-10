// path_to_text exists for one reason: std::filesystem::path::string() is
// platform-divergent (backslashes and code-page narrowing on Windows). These
// assertions pin the canonical conversion on every platform, so the Windows CI
// leg proves the divergence is actually neutralized.

#include "nucleus/configuration_source/path_text.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <filesystem>

TEST_CASE("path_to_text yields forward-slash separators on every platform",
          "[path_text]")
{
    const std::filesystem::path p = std::filesystem::path("conf") / "site" / "base.xml";
    REQUIRE(nucleus::path_to_text(p) == "conf/site/base.xml");
}

TEST_CASE("path_to_text preserves non-ASCII text as UTF-8", "[path_text]")
{
    // \u escapes keep the test independent of the source file's encoding (an
    // MSVC build without /utf-8 would double-encode a literal non-ASCII byte).
    const std::filesystem::path p(u8"konfigurasjon/\u00e6\u00f8\u00e5.xml");
    REQUIRE(nucleus::path_to_text(p) == "konfigurasjon/\xc3\xa6\xc3\xb8\xc3\xa5.xml");
}

TEST_CASE("path_to_text round-trips through a reconstructed path", "[path_text]")
{
    const std::filesystem::path original = std::filesystem::path("a") / "b" / "c.xml";
    const std::filesystem::path rebuilt(nucleus::path_to_text(original));
    REQUIRE(nucleus::path_to_text(rebuilt) == nucleus::path_to_text(original));
}

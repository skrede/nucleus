#include "nucleus/utility/escaped_text.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <cstddef>
#include <cstdint>

// The escaper is the seam every rejection message quotes untrusted text through, so
// it is held to two properties: no byte outside printable ASCII survives it, and the
// encoding is injective -- an escape in the output always came from the byte it names.

TEST_CASE("no C0 control byte survives the escaper", "[utility][escaped]")
{
    for(std::int32_t byte = 0x00; byte < 0x20; ++byte)
    {
        const std::string out = nucleus::escaped_text(std::string(1, static_cast<char>(byte)));
        CAPTURE(byte, out);
        REQUIRE(out.front() == '\\');
        REQUIRE(out.find(static_cast<char>(byte)) == std::string::npos);
    }
}

TEST_CASE("DEL and every byte above ASCII leave the escaper in hex form", "[utility][escaped]")
{
    for(std::int32_t byte = 0x7f; byte <= 0xff; ++byte)
    {
        const std::string out = nucleus::escaped_text(std::string(1, static_cast<char>(byte)));
        CAPTURE(byte, out);
        REQUIRE(out.size() == 4);
        REQUIRE(out.starts_with("\\x"));
    }
}

TEST_CASE("a bidirectional override and an eight-bit control byte are escaped", "[utility][escaped]")
{
    // U+202E RIGHT-TO-LEFT OVERRIDE (CVE-2021-42574) reorders rendered text without
    // changing it; 0x9b is CSI in an eight-bit-clean terminal.
    std::string csi = "a";
    csi.push_back('\x9b');
    csi += "31mRED";

    REQUIRE(nucleus::escaped_text("safe\xe2\x80\xaexes") == "safe\\xe2\\x80\\xaexes");
    REQUIRE(nucleus::escaped_text(csi) == "a\\x9b31mRED");
}

TEST_CASE("the escape character is itself escaped", "[utility][escaped]")
{
    REQUIRE(nucleus::escaped_text("my\\ntool") == "my\\\\ntool");
    REQUIRE(nucleus::escaped_text("my\ntool") == "my\\ntool");
    REQUIRE(nucleus::escaped_text("my\\ntool") != nucleus::escaped_text("my\ntool"));
}

TEST_CASE("no two texts share an escaped form", "[utility][escaped]")
{
    std::set<std::string> seen;
    for(std::int32_t high = 0x00; high <= 0xff; ++high)
    {
        seen.insert(nucleus::escaped_text(std::string(1, static_cast<char>(high))));
        for(std::int32_t low = 0x00; low <= 0xff; ++low)
        {
            std::string text;
            text.push_back(static_cast<char>(high));
            text.push_back(static_cast<char>(low));
            seen.insert(nucleus::escaped_text(text));
        }
    }

    REQUIRE(seen.size() == std::size_t{0x100} + std::size_t{0x10000});
}

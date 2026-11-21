#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/expected.h"

#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <utility>

namespace {

std::pair<nucleus::expected<void, nucleus::error>, std::string>
emit_one_schema_blind(const std::string &key, const std::string &value)
{
    std::map<std::string, std::string> values{{key, value}};
    auto                               made = nucleus::config::from_values(std::move(values));
    REQUIRE(made);
    const nucleus::config cfg = std::move(made).value();
    std::ostringstream    out;
    auto                  result = nucleus::xml::emit_document_schema_blind(cfg, out);
    return {std::move(result), out.str()};
}

}

TEST_CASE("xml emit rejects a key segment that is not a valid XML name",
          "[emit][seam][xml]")
{
    // Every hostile ASCII case the write boundary must catch, as an element name
    // (leading segment) and as a leaf name.
    const std::vector<std::string> hostile_keys{
            "ser ver/host",  // space
            "server/ho st",  // space in the leaf
            "ser<ver/host",  // '<'
            "server/ho\"st", // '"'
            "9server/host",  // leading digit
            "server/0host"}; // leading digit in the leaf

    for(const std::string &key : hostile_keys)
    {
        auto [result, emitted] = emit_one_schema_blind(key, "value");
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code == nucleus::errc::malformed_source);
        // All-or-nothing: nothing reached the stream.
        REQUIRE(emitted.empty());
    }
}

TEST_CASE("xml emit rejects a value carrying a control byte or a bare CR",
          "[emit][seam][xml]")
{
    for(const std::string &value : {std::string("a\x01"
                                                "b"),
                                    std::string("a\rb")})
    {
        auto [result, emitted] = emit_one_schema_blind("server/host", value);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().code == nucleus::errc::malformed_source);
        REQUIRE(emitted.empty());
    }
}

TEST_CASE("xml emit accepts a high-byte (UTF-8) name and a newline-bearing value",
          "[emit][seam][xml]")
{
    // A legal international element name is permitted un-decoded.
    auto [name_ok, name_out] = emit_one_schema_blind("caf\xc3\xa9/host", "value");
    REQUIRE(name_ok);
    REQUIRE(name_out.find("caf\xc3\xa9") != std::string::npos);

    // Tab and newline are legal XML characters and round-trip-stable, so a value
    // carrying them still emits (only bare CR and other C0 controls are refused).
    auto [value_ok, value_out] = emit_one_schema_blind(
            "server/host", "line1\nline2\ttab");
    REQUIRE(value_ok);
    REQUIRE(value_out.find("line1") != std::string::npos);
}

#include "nucleus/config.h"

#include "nucleus/xml/xml_grammar.h"
#include "nucleus/xml/xml_emitter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <utility>
#include <string_view>

namespace {

nucleus::config config_of(std::string key, std::string value)
{
    auto made = nucleus::config::from_values({{std::move(key), std::move(value)}});
    REQUIRE(made);
    return std::move(made).value();
}

void check_key(std::string key, bool accepted)
{
    const auto rendered = nucleus::xml::render_document_schema_blind(
            config_of(std::move(key), "v"));
    if(accepted)
    {
        REQUIRE(rendered);
        return;
    }
    REQUIRE_FALSE(rendered);
    CHECK(rendered.error().code == nucleus::errc::malformed_source);
    CHECK(rendered.error().message.find("not a valid XML name") != std::string::npos);
}

void check_name_start(std::string_view start, bool accepted)
{
    check_key(std::string(start) + "x", accepted);
}

void check_name_tail(std::string_view tail, bool accepted)
{
    check_key("x" + std::string(tail), accepted);
}

void check_text(std::string_view text, bool accepted)
{
    const auto rendered = nucleus::xml::render_document_schema_blind(
            config_of("item", std::string(text)));
    if(accepted)
    {
        REQUIRE(rendered);
        return;
    }
    REQUIRE_FALSE(rendered);
    CHECK(rendered.error().code == nucleus::errc::malformed_source);
    CHECK(rendered.error().message.find("XML cannot represent") != std::string::npos);
}

}

TEST_CASE("XML emission rejects every malformed UTF-8 class",
          "[xml][emit][grammar][matrix]")
{
    const std::vector<std::string_view> malformed{
            "\x80",             // lone continuation
            "\xBF",             // lone continuation
            "\xC3",             // truncated two-byte
            "\xE2\x82",         // truncated three-byte
            "\xF0\x9F\x98",     // truncated four-byte
            "\xC3\x28",         // invalid continuation
            "\xC0\xAF",         // overlong two-byte
            "\xE0\x80\xAF",     // overlong three-byte
            "\xF0\x80\x80\xAF", // overlong four-byte
            "\xED\xA0\x80",     // U+D800 encoded as UTF-8
            "\xED\xBF\xBF",     // U+DFFF encoded as UTF-8
            "\xF4\x90\x80\x80", // U+110000
            "\xF5\x80\x80\x80", // lead byte past the encodable domain
            "\xFE",             // never a UTF-8 lead byte
            "\xFF"};            // never a UTF-8 lead byte
    for(const std::string_view sequence : malformed)
    {
        check_name_start(sequence, false);
        check_text(sequence, false);
    }
}

TEST_CASE("XML names start on exactly the NameStartChar production",
          "[xml][emit][grammar][matrix]")
{
    const std::vector<std::pair<std::string_view, bool>> starts{
            {":", true},
            {"_", true},
            {"A", true},
            {"z", true},
            {"\xC3\x80", true},          // U+00C0
            {"\xC3\x97", false},         // U+00D7, the U+00D6/U+00D8 gap
            {"\xCD\xBE", false},         // U+037E, the U+037D/U+037F gap
            {"\xE2\x80\x8C", true},      // U+200C
            {"\xEF\xB7\x90", false},     // U+FDD0, the U+FDCF/U+FDF0 gap
            {"\xEF\xBF\xBD", true},      // U+FFFD
            {"\xF0\x90\x80\x80", true},  // U+10000
            {"\xF3\xAF\xBF\xBF", true},  // U+EFFFF
            {"\xF3\xB0\x80\x80", false}, // U+F0000, past the name domain
            {"-", false},
            {".", false},
            {"0", false},
            {"\xC2\xB7", false},      // U+00B7
            {"\xCC\x80", false},      // U+0300
            {"\xE2\x80\xBF", false}}; // U+203F
    for(const auto &[start, accepted] : starts)
        check_name_start(start, accepted);
}

TEST_CASE("XML names accept the NameChar additions only after the first character",
          "[xml][emit][grammar][matrix]")
{
    const std::vector<std::pair<std::string_view, bool>> tails{
            {"-", true},
            {".", true},
            {"9", true},
            {"\xC2\xB7", true},      // U+00B7
            {"\xCC\x80", true},      // U+0300
            {"\xCD\xAF", true},      // U+036F
            {"\xE2\x80\xBF", true},  // U+203F
            {"\xE2\x81\x80", true},  // U+2040
            {"\xE2\x81\x81", false}, // U+2041, just past the addition
            {"\xCD\xBE", false}};    // U+037E
    for(const auto &[tail, accepted] : tails)
        check_name_tail(tail, accepted);
}

TEST_CASE("XML character data accepts the Char production and rejects its gaps",
          "[xml][emit][grammar][matrix]")
{
    const std::vector<std::pair<std::string_view, bool>> texts{
            {"a\tb", true},
            {"a\nb", true},
            {" ", true},
            {"\xED\x9F\xBF", true},     // U+D7FF
            {"\xEE\x80\x80", true},     // U+E000
            {"\xEF\xBF\xBD", true},     // U+FFFD
            {"\xF0\x90\x80\x80", true}, // U+10000
            {"\xF4\x8F\xBF\xBF", true}, // U+10FFFF
            {"a\x01"
             "b",
             false},
            {"a\x0B"
             "b",
             false},
            {"\xEF\xBF\xBE", false},  // U+FFFE
            {"\xEF\xBF\xBF", false}}; // U+FFFF
    for(const auto &[text, accepted] : texts)
        check_text(text, accepted);
}

TEST_CASE("the strict UTF-8 decoder rejects the sequences no code point owns",
          "[xml][emit][grammar][matrix]")
{
    const std::vector<std::pair<std::string_view, bool>> sequences{
            {"A", true},
            {"\xC2\xA9", true},          // U+00A9
            {"\xED\x9F\xBF", true},      // U+D7FF, the last point before the surrogates
            {"\xED\xA0\x80", false},     // U+D800 has no UTF-8 encoding
            {"\xED\xBF\xBF", false},     // U+DFFF has no UTF-8 encoding
            {"\xF4\x8F\xBF\xBF", true},  // U+10FFFF
            {"\xF4\x90\x80\x80", false}, // U+110000
            {"\xF5\x80\x80\x80", false}, // lead byte past the encodable domain
            {"\xC0\xAF", false},         // overlong two-byte
            {"\xE0\x80\xAF", false}};    // overlong three-byte
    for(const auto &[sequence, decodable] : sequences)
        CHECK(nucleus::xml::decode_utf8(sequence, 0).has_value() == decodable);
}

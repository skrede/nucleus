#include "nucleus/utility/display_controls.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace {

std::string byte_text(std::uint8_t byte)
{
    return std::string(1, static_cast<char>(byte));
}

const std::vector<std::string> bidi_controls{"\xd8\x9c", "\xe2\x80\x8e", "\xe2\x80\x8f", "\xe2\x80\xaa",
                                             "\xe2\x80\xab", "\xe2\x80\xac", "\xe2\x80\xad", "\xe2\x80\xae",
                                             "\xe2\x81\xa6", "\xe2\x81\xa7", "\xe2\x81\xa8", "\xe2\x81\xa9"};

}

TEST_CASE("a well-formed sequence is taken whole and an ill-formed lead alone", "[utility][display]")
{
    const std::vector<std::pair<std::string, std::string>> rows{
            {"\xc2\x9bz", "\xc2\x9b"},
            {"\xc2z", "\xc2"},
            {"\xe0\x9b\x80", "\xe0"},
            {"\xf0\x9f\x98\x80!", "\xf0\x9f\x98\x80"},
            {"az", "a"},
            {"", ""}};
    for(const auto &[text, sequence] : rows)
    {
        CAPTURE(text);
        REQUIRE(nucleus::detail::display_sequence(text) == sequence);
    }
}

TEST_CASE("C0, DEL, C1 and every Bidi_Control code point are display controls", "[utility][display]")
{
    std::vector<std::string> controls{byte_text(0x00), byte_text(0x1b), byte_text(0x1f), byte_text(0x7f),
                                      "\xc2\x80", "\xc2\x85", "\xc2\x9b", "\xc2\x9f", byte_text(0x9b)};
    controls.insert(controls.end(), bidi_controls.begin(), bidi_controls.end());
    REQUIRE(bidi_controls.size() == 12);
    for(const std::string &control : controls)
    {
        CAPTURE(control);
        REQUIRE(nucleus::detail::is_display_control(control));
    }
}

TEST_CASE("printable ASCII, prose and an ill-formed lead are no display controls", "[utility][display]")
{
    const std::vector<std::string> prose{"a", " ", "~", "\xc2\xa0", "\xc3\xa9", "\xe2\x80\xa9",
                                         "\xe2\x9b\x80", "\xf0\x9f\x98\x80", byte_text(0xc2)};
    for(const std::string &text : prose)
    {
        CAPTURE(text);
        REQUIRE_FALSE(nucleus::detail::is_display_control(text));
    }
}

TEST_CASE("a text holds a display control only when one of its sequences is one", "[utility][display]")
{
    const std::vector<std::string> clean{"caf\xc3\xa9", std::string("gr\xc3\xb6\xc3\x9f") + "e", "has space", "x;y", ""};
    const std::vector<std::string> tainted{std::string("a\x1b") + "b", "x\xe2\x80\xaey", "x\xc2\x9by", "x\x9by", "a\nb"};
    for(const std::string &text : clean)
    {
        CAPTURE(text);
        REQUIRE_FALSE(nucleus::detail::has_display_control(text));
    }
    for(const std::string &text : tainted)
    {
        CAPTURE(text);
        REQUIRE(nucleus::detail::has_display_control(text));
    }
}

TEST_CASE("displayed text escapes each control sequence and passes the rest", "[utility][display]")
{
    const std::vector<std::pair<std::string, std::string>> rows{
            {"a\x1b[2Jb", R"(a\x1b[2Jb)"},
            {"x\xe2\x80\xaey", R"(x\xe2\x80\xaey)"},
            {"caf\xc3\xa9", "caf\xc3\xa9"},
            {"a\nb", R"(a\nb)"},
            {"a\xe2\x80\xa9z", "a\xe2\x80\xa9z"}};
    for(const auto &[text, displayed] : rows)
    {
        CAPTURE(text);
        REQUIRE(nucleus::detail::displayed_text(text) == displayed);
    }
}

#ifndef HPP_GUARD_NUCLEUS_UTILITY_DISPLAY_CONTROLS_H
#define HPP_GUARD_NUCLEUS_UTILITY_DISPLAY_CONTROLS_H

#include "nucleus/utility/escaped_text.h"

#include <array>
#include <string>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nucleus::detail {

struct utf8_form
{
    std::uint8_t lead_low;
    std::uint8_t lead_high;
    std::uint8_t first_tail_low;
    std::uint8_t first_tail_high;
    std::size_t  tail_count;
};

// Unicode 15.0, table 3-7 "Well-Formed UTF-8 Byte Sequences".
inline constexpr std::array<utf8_form, 9> utf8_forms{
        {{0x00, 0x7F, 0x00, 0x00, 0},
         {0xC2, 0xDF, 0x80, 0xBF, 1},
         {0xE0, 0xE0, 0xA0, 0xBF, 2},
         {0xE1, 0xEC, 0x80, 0xBF, 2},
         {0xED, 0xED, 0x80, 0x9F, 2},
         {0xEE, 0xEF, 0x80, 0xBF, 2},
         {0xF0, 0xF0, 0x90, 0xBF, 3},
         {0xF1, 0xF3, 0x80, 0xBF, 3},
         {0xF4, 0xF4, 0x80, 0x8F, 3}}};

inline bool has_utf8_tail(std::string_view bytes, const utf8_form &form)
{
    if(bytes.size() <= form.tail_count)
        return false;
    for(std::size_t i = 1; i <= form.tail_count; ++i)
    {
        const auto         byte = static_cast<std::uint8_t>(bytes[i]);
        const std::uint8_t low  = i == 1 ? form.first_tail_low : std::uint8_t{0x80};
        const std::uint8_t high = i == 1 ? form.first_tail_high : std::uint8_t{0xBF};
        if(byte < low || byte > high)
            return false;
    }
    return true;
}

inline std::size_t utf8_sequence_length(std::string_view bytes)
{
    if(bytes.empty())
        return 0;
    const auto lead = static_cast<std::uint8_t>(bytes[0]);
    for(const utf8_form &form : utf8_forms)
    {
        if(lead >= form.lead_low && lead <= form.lead_high)
            return has_utf8_tail(bytes, form) ? form.tail_count + 1 : 0;
    }
    return 0;
}

inline bool is_c1_control(std::string_view sequence)
{
    if(sequence.size() != 2 || static_cast<std::uint8_t>(sequence[0]) != 0xc2)
        return false;
    const auto second = static_cast<std::uint8_t>(sequence[1]);
    return second >= 0x80 && second <= 0x9f;
}

// The Bidi_Control property of the Unicode Character Database's PropList.txt: U+061C, U+200E, U+200F,
// U+202A-U+202E and U+2066-U+2069. Compared by byte because misc-misleading-bidirectional refuses a
// string literal that holds them.
inline bool is_bidi_control(std::string_view bytes)
{
    if(bytes.size() == 2)
        return static_cast<std::uint8_t>(bytes[0]) == 0xd8 && static_cast<std::uint8_t>(bytes[1]) == 0x9c;
    if(bytes.size() != 3 || static_cast<std::uint8_t>(bytes[0]) != 0xe2)
        return false;
    const auto second = static_cast<std::uint8_t>(bytes[1]);
    const auto third  = static_cast<std::uint8_t>(bytes[2]);
    if(second == 0x80)
        return third == 0x8e || third == 0x8f || (third >= 0xaa && third <= 0xae);
    return second == 0x81 && third >= 0xa6 && third <= 0xa9;
}

inline std::string_view display_sequence(std::string_view text)
{
    const std::size_t length = utf8_sequence_length(text);
    return text.substr(0, length == 0 ? 1 : length);
}

// A lone byte in 0x80-0x9f starts no well-formed sequence, so it is the C1 control itself.
inline bool is_display_control(std::string_view sequence)
{
    if(sequence.size() == 1)
    {
        const auto byte = static_cast<std::uint8_t>(sequence[0]);
        return byte < 0x20 || byte == 0x7f || (byte >= 0x80 && byte <= 0x9f);
    }
    return is_c1_control(sequence) || is_bidi_control(sequence);
}

inline bool has_display_control(std::string_view text)
{
    while(!text.empty())
    {
        const std::string_view sequence = display_sequence(text);
        if(is_display_control(sequence))
            return true;
        text.remove_prefix(sequence.size());
    }
    return false;
}

inline std::string displayed_text(std::string_view text)
{
    std::string out;
    while(!text.empty())
    {
        const std::string_view sequence = display_sequence(text);
        if(is_display_control(sequence))
            out += escaped_text(sequence);
        else
            out += sequence;
        text.remove_prefix(sequence.size());
    }
    return out;
}

}

#endif

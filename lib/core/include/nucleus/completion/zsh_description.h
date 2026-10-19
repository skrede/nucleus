#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_DESCRIPTION_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_DESCRIPTION_H

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

// U+202A-U+202E and U+2066-U+2069 in UTF-8, compared by byte because misc-misleading-bidirectional
// refuses a string literal that holds them.
inline bool is_bidi_control(std::string_view bytes)
{
    if(bytes.size() != 3 || static_cast<std::uint8_t>(bytes[0]) != 0xe2)
        return false;
    const auto second = static_cast<std::uint8_t>(bytes[1]);
    const auto third  = static_cast<std::uint8_t>(bytes[2]);
    return (second == 0x80 && third >= 0xaa && third <= 0xae) || (second == 0x81 && third >= 0xa6 && third <= 0xa9);
}

inline void append_escaped(std::string &out, char c)
{
    static constexpr std::string_view structural = "\\[]:";
    if(structural.find(c) != std::string_view::npos)
        out.push_back('\\');
    out.push_back(c);
}

inline void append_displayed(std::string &out, char c)
{
    const auto byte = static_cast<std::uint8_t>(c);
    if(byte < 0x20 || byte == 0x7f)
    {
        out.push_back('^');
        append_escaped(out, static_cast<char>(byte ^ 0x40));
    }
    else
        append_escaped(out, c);
}

inline void append_rendered(std::string &out, std::string_view bytes)
{
    for(char const c : escaped_text(bytes))
        append_escaped(out, c);
}

// `\ [ ] :` are what `_arguments` reads as structure around a description and are backslashed, an
// unescaped `]` being the one route into the action field it evaluates. C0 and DEL take caret form so a
// newline cannot split the spec; the C1 controls, as UTF-8 and as a bare byte outside a well-formed
// sequence, and the nine CVE-2021-42574 controls take escaped_text's `\xNN` form. Every other byte
// passes unchanged, so non-ASCII prose stays readable.
inline std::string zsh_description(std::string_view text)
{
    std::string out;
    std::size_t i = 0;
    while(i < text.size())
    {
        const std::size_t      length   = utf8_sequence_length(text.substr(i));
        const std::string_view sequence = text.substr(i, length == 0 ? 1 : length);
        const auto             lead     = static_cast<std::uint8_t>(sequence[0]);
        const bool             bare_c1  = length == 0 && lead >= 0x80 && lead <= 0x9f;
        if(bare_c1 || is_c1_control(sequence) || is_bidi_control(sequence))
            append_rendered(out, sequence);
        else if(length == 1)
            append_displayed(out, sequence[0]);
        else
            out += sequence;
        i += sequence.size();
    }
    return out;
}

}

#endif

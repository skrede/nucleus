#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_DESCRIPTION_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_DESCRIPTION_H

#include "nucleus/utility/escaped_text.h"
#include "nucleus/utility/display_controls.h"

#include <string>
#include <cstdint>
#include <string_view>

namespace nucleus::detail {

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
// sequence, and the Unicode Bidi_Control property take escaped_text's `\xNN` form. Every other byte
// passes unchanged, so non-ASCII prose stays readable. The rule assumes a UTF-8 terminal, where a
// 0x80-0x9f byte inside a well-formed sequence is no control.
inline std::string zsh_description(std::string_view text)
{
    std::string out;
    while(!text.empty())
    {
        const std::string_view sequence = display_sequence(text);
        if(sequence.size() == 1 && static_cast<std::uint8_t>(sequence[0]) < 0x80)
            append_displayed(out, sequence[0]);
        else if(is_display_control(sequence))
            append_rendered(out, sequence);
        else
            out += sequence;
        text.remove_prefix(sequence.size());
    }
    return out;
}

}

#endif

#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_QUOTED_FLAG_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_QUOTED_FLAG_H

#include "nucleus/utility/display_controls.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nucleus::detail {

inline bool zsh_quotes_sequence(std::string_view sequence)
{
    static constexpr std::string_view quoted_punctuation = " !\"#$&'()*;<>?[\\]^`{|}~";
    if(sequence.size() > 1)
        return is_c1_control(sequence) || is_bidi_control(sequence);
    const auto byte = static_cast<std::uint8_t>(sequence[0]);
    return byte < 0x20 || byte >= 0x7f || quoted_punctuation.find(sequence[0]) != std::string_view::npos;
}

// zsh 5.9 quotes these characters when it inserts a flag, and releases before it insert them unquoted. The set was
// measured per byte in a UTF-8 locale: control bytes, the punctuation above, C1 and Bidi_Control, and malformed UTF-8.
inline bool zsh_quotes_on_insert(std::string_view text)
{
    for(std::size_t at = 0; at < text.size();)
    {
        const std::string_view sequence = display_sequence(text.substr(at));
        if(zsh_quotes_sequence(sequence))
            return true;
        at += sequence.size();
    }
    return false;
}

}

#endif

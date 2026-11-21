#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_SAFE_ASCII_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_SAFE_ASCII_H

#include <cstdint>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

// zsh 5.9 does not insert the ASCII bytes outside this set as typed, and judges a non-ASCII character by the C
// library's printability, which differs by platform and locale; so any byte outside the set counts.
inline bool zsh_flag_outside_safe_ascii(std::string_view text)
{
    static constexpr std::string_view syntax_punctuation = " !\"#$&'()*;<>?[\\]^`{|}~";
    return std::ranges::any_of(text, [](char c)
                               {
        const auto byte = static_cast<std::uint8_t>(c);
        return byte < 0x20 || byte >= 0x7f || syntax_punctuation.find(c) != std::string_view::npos; });
}

}

#endif

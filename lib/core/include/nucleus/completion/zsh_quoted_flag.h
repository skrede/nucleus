#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_QUOTED_FLAG_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_QUOTED_FLAG_H

#include <cstdint>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

// Every byte outside printable, non-syntax ASCII counts as quoted. zsh 5.9 quotes those ASCII bytes on insertion,
// and judges a non-ASCII character by the C library's printability, which differs by platform and locale.
inline bool zsh_quotes_on_insert(std::string_view text)
{
    static constexpr std::string_view quoted_punctuation = " !\"#$&'()*;<>?[\\]^`{|}~";
    return std::ranges::any_of(text, [](char c)
                               {
        const auto byte = static_cast<std::uint8_t>(c);
        return byte < 0x20 || byte >= 0x7f || quoted_punctuation.find(c) != std::string_view::npos; });
}

}

#endif

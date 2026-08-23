#ifndef HPP_GUARD_NUCLEUS_UTILITY_ESCAPED_TEXT_H
#define HPP_GUARD_NUCLEUS_UTILITY_ESCAPED_TEXT_H

#include <string>
#include <cstdint>
#include <string_view>

namespace nucleus::detail {

inline constexpr std::string_view hex_digits = "0123456789abcdef";

inline char escape_abbreviation(char c)
{
    if(c == '\n')
        return 'n';
    if(c == '\r')
        return 'r';
    if(c == '\t')
        return 't';
    if(c == '\\')
        return '\\';
    return '\0';
}

}

namespace nucleus {

// Renders every byte outside printable ASCII as an escape so a diagnostic that quotes
// untrusted text cannot forge a log line or drive the reader's terminal through the
// very message it triggered. Bytes above 0x7f are escaped too: they carry the eight-bit
// C1 controls and the bidirectional overrides of the Trojan Source attack
// (CVE-2021-42574), which reorder rendered text without changing it. The backslash is
// escaped as well, which is what makes the encoding injective -- an escape in the output
// always came from the byte it names, never from a token that merely spelled it.
inline std::string escaped_text(std::string_view text)
{
    std::string out;
    for(char const c : text)
    {
        const auto byte = static_cast<std::uint8_t>(c);
        if(const char abbrev = detail::escape_abbreviation(c); abbrev != '\0')
        {
            out.push_back('\\');
            out.push_back(abbrev);
        }
        else if(byte < 0x20 || byte >= 0x7f)
        {
            out += "\\x";
            out.push_back(detail::hex_digits[byte >> 4]);
            out.push_back(detail::hex_digits[byte & 0x0f]);
        }
        else
            out.push_back(c);
    }
    return out;
}

}

#endif

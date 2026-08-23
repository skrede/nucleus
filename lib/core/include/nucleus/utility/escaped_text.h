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
    return '\0';
}

}

namespace nucleus {

// Renders control characters as escapes so a diagnostic that quotes untrusted text
// cannot forge a log line or drive the reader's terminal through the very message
// it triggered. Printable bytes survive byte-for-byte, which is what keeps the
// quoted text recognizable as the offending input.
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
        else if(byte < 0x20 || byte == 0x7f)
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

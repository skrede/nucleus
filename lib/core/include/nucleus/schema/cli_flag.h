#ifndef HPP_GUARD_NUCLEUS_SCHEMA_CLI_FLAG_H
#define HPP_GUARD_NUCLEUS_SCHEMA_CLI_FLAG_H

#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <utility>
#include <string_view>

namespace nucleus {

// The flag-side separator of the CLI bijection: the string standing in for the
// keyspace separator `/` inside a flag (`--a-b-c` <-> `a/b/c`). A validated value
// type, so a held delimiter is always usable: never empty, never containing `=`
// (the key/value split would eat it), and never containing `/` unless it IS `/`
// (the identity mapping). Multi-character delimiters such as `__` are legal.
//
// Invertibility stays the host's contract: no schema segment may contain the
// chosen delimiter as a substring, exactly as segments may not contain `-` under
// the default.
class cli_delimiter
{
public:
    cli_delimiter() = default; // the conventional `-`

    static expected<cli_delimiter, std::string> parse(std::string_view text)
    {
        if(text.empty())
            return unexpected(std::string("CLI delimiter is empty"));
        if(text.find('=') != std::string_view::npos)
            return unexpected(std::string("CLI delimiter '") + std::string(text)
                        + "' must not contain '='");
        if(text.size() > 1 && text.find(key_path::separator) != std::string_view::npos)
            return unexpected(std::string("CLI delimiter '") + std::string(text)
                        + "' must not contain the keyspace separator '/'");
        if(text.find('[') != std::string_view::npos
                || text.find(']') != std::string_view::npos)
            return unexpected(std::string("CLI delimiter '") + std::string(text)
                        + "' must not contain '[' or ']' (ordinal-index notation)");
        bool all_digits = true;
        for(char c : text)
            all_digits = all_digits && (c >= '0' && c <= '9');
        if(all_digits)
            return unexpected(std::string("CLI delimiter '") + std::string(text)
                        + "' must not be all digits (ordinal-index notation)");
        return cli_delimiter(std::string(text));
    }

    const std::string &str() const noexcept { return m_text; }

    // True for the `/` delimiter, where flag body and key path are one string.
    bool is_separator() const noexcept
    {
        return m_text.size() == 1 && m_text.front() == key_path::separator;
    }

    friend bool operator==(const cli_delimiter &a, const cli_delimiter &b) noexcept
    {
        return a.m_text == b.m_text;
    }

private:
    explicit cli_delimiter(std::string text)
        : m_text(std::move(text))
    {
    }

    std::string m_text = "-";
};

// The inverse projection: a keyspace path back to its canonical CLI flag under the
// given delimiter. Because no segment may contain the delimiter, this is a total,
// lossless inverse of the segmentation -- the bijection made explicit and the basis
// for the schema-projected flag surface and tab completion. Core's bijection
// authority.
inline std::string flag_of(const key_path &path,
                                         const cli_delimiter &delimiter = {})
{
    std::string flag = "--";
    const auto &segments = path.segments();
    for(std::size_t i = 0; i < segments.size(); ++i)
    {
        if(i != 0)
            flag.append(delimiter.str());
        flag.append(segments[i]);
    }
    return flag;
}

}

#endif

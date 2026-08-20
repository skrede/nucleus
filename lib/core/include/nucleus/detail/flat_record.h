#ifndef HPP_GUARD_NUCLEUS_DETAIL_FLAT_RECORD_H
#define HPP_GUARD_NUCLEUS_DETAIL_FLAT_RECORD_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace nucleus::detail {

// One selected key together with everything that shares its line: held whole so a
// selection can be proven renderable before any of it is spelled out.
struct flat_record
{
    std::string              key;
    std::vector<std::string> values;
};

struct flat_template_record
{
    std::string key;
    std::string annotation;
};

inline bool has_flat_line_break(std::string_view text) noexcept
{
    return text.find('\n') != std::string_view::npos || text.find('\r') != std::string_view::npos;
}

// A key component must survive the record's own punctuation: `=` would move the
// key/value split, and CR or LF would end the record early. A value carries no
// such constraint on `=`, which is ordinary data to the right of the split.
inline expected<void, error> validate_flat_key_bytes(std::string_view role,
                                                     std::string_view text)
{
    if(!has_flat_line_break(text) && text.find('=') == std::string_view::npos)
        return {};
    return unexpected(error{errc::malformed_source, nucleus::format("flat render: {} '{}' carries '=', a newline or a carriage return", role, text)});
}

inline expected<void, error> validate_flat_key(std::string_view key)
{
    return validate_flat_key_bytes("key", key);
}

inline expected<void, error> validate_flat_prefix(std::string_view prefix)
{
    return validate_flat_key_bytes("record prefix", prefix);
}

inline expected<void, error> validate_flat_value(std::string_view key,
                                                 std::string_view value)
{
    if(!has_flat_line_break(value))
        return {};
    return unexpected(error{errc::malformed_source, nucleus::format("flat render: value for key '{}' carries an embedded newline or carriage "
                                                                    "return",
                                                                    key)});
}

inline void append_flat_line(std::string &output, std::string_view prefix,
                             std::string_view key, std::string_view tail)
{
    output.append(prefix);
    output.append(key);
    output.push_back('=');
    output.append(tail);
    output.push_back('\n');
}

inline std::string allowed_values_annotation(const std::vector<std::string> &allowed)
{
    if(allowed.empty())
        return {};
    std::string text = " # allowed: ";
    for(std::size_t i = 0; i < allowed.size(); ++i)
    {
        if(i != 0)
            text.push_back('|');
        text.append(allowed[i]);
    }
    return text;
}

}

#endif

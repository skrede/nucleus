#ifndef HPP_GUARD_NUCLEUS_COMPLETION_PROGRAM_TOKEN_H
#define HPP_GUARD_NUCLEUS_COMPLETION_PROGRAM_TOKEN_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/utility/escaped_text.h"

#include <cstdint>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus {

// Classified by explicit ASCII ranges rather than <cctype>: the classifiers are
// locale-sensitive, and a locale that widened the accepted set would widen a
// trust boundary along with it.
inline bool is_program_token_lead(char c)
{
    const auto byte = static_cast<std::uint8_t>(c);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
        || (byte >= '0' && byte <= '9') || c == '_';
}

inline bool is_program_token_char(char c)
{
    return is_program_token_lead(c) || c == '.' || c == '-';
}

// The generated script writes the program name at shell command position with no
// quoting, so the accepted grammar is a bare command token and nothing else --
// every path separator, whitespace character and shell metacharacter is refused
// before any script text exists. The first byte is held narrower than the rest:
// `complete -F fn <prog>` and `#compdef <prog>` both read a leading '-' as an
// option of the command the name was meant to occupy, and a leading '.' names a
// path reference rather than a command.
inline expected<void, error> check_program_token(std::string_view prog)
{
    const bool opens_as_command = !prog.empty() && is_program_token_lead(prog.front());
    if(opens_as_command && std::ranges::all_of(prog, is_program_token_char))
        return {};
    return unexpected(error{errc::malformed_source,
        nucleus::format("program name '{}' is not a bare command token: a program "
                        "name opens with a letter, digit or '_' and carries only "
                        "letters, digits, '.', '_' and '-' (it reaches shell "
                        "command position unquoted)",
                        escaped_text(prog))});
}

// The space name becomes the leading segments of every completion entry, so a
// well-formed name is exactly what the path parser accepts; a second predicate
// beside it could drift and re-admit a name the join then carries through
// unchecked. The parser's own message is not forwarded -- it quotes the offending
// text raw, which is the forgery every diagnostic here escapes.
inline expected<key_path, error> check_space_name(std::string_view space_name)
{
    if(space_name.empty())
        return key_path{};
    if(auto parsed = key_path::parse(space_name); parsed)
        return *std::move(parsed);
    return unexpected(error{errc::malformed_source,
        nucleus::format("space name '{}' is not a well-formed key path: a path carries "
                        "no empty segment and no leading or trailing '/', and a "
                        "bracketed segment names a non-empty base and an ordinal of at "
                        "most ten digits",
                        escaped_text(space_name))});
}

}

#endif

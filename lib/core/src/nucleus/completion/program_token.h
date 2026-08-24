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

inline bool is_space_segment(std::string_view segment)
{
    return !segment.empty() && is_program_token_lead(segment.front())
        && std::ranges::all_of(segment, is_program_token_char);
}

// The space name becomes the leading segments of every completion entry, so it lands in
// the same emitted word list the flags do -- and a word list is expanded, not merely
// split. The path parser constrains a path's SHAPE and never the bytes a segment
// carries, so each segment is held to the bare command-token grammar beside it rather
// than to the parser alone. The parser's own message is not forwarded -- it quotes the
// offending text raw, which is the forgery every diagnostic here escapes.
inline expected<key_path, error> check_space_name(std::string_view space_name)
{
    if(space_name.empty())
        return key_path{};
    auto parsed = key_path::parse(space_name);
    if(parsed && std::ranges::all_of(parsed->segments(), is_space_segment))
        return *std::move(parsed);
    return unexpected(error{errc::malformed_source,
        nucleus::format("space name '{}' is not a path of bare command tokens: a path carries "
                        "no empty segment and no leading or trailing '/', and each segment "
                        "opens with a letter, digit or '_' and carries only letters, digits, "
                        "'.', '_' and '-'",
                        escaped_text(space_name))});
}

}

#endif

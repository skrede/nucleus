#ifndef HPP_GUARD_NUCLEUS_COMPLETION_PROGRAM_TOKEN_H
#define HPP_GUARD_NUCLEUS_COMPLETION_PROGRAM_TOKEN_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/utility/escaped_text.h"

#include <cstdint>
#include <algorithm>
#include <string_view>

namespace nucleus {

// Classified by explicit ASCII ranges rather than <cctype>: the classifiers are
// locale-sensitive, and a locale that widened the accepted set would widen a
// trust boundary along with it.
inline bool is_program_token_char(char c)
{
    const auto byte = static_cast<std::uint8_t>(c);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
        || (byte >= '0' && byte <= '9') || c == '.' || c == '_' || c == '-';
}

// The generated script writes the program name at shell command position with no
// quoting, so the accepted grammar is a bare command token and nothing else --
// every path separator, whitespace character and shell metacharacter is refused
// before any script text exists.
inline expected<void, error> check_program_token(std::string_view prog)
{
    if(!prog.empty() && std::ranges::all_of(prog, is_program_token_char))
        return {};
    return unexpected(error{errc::malformed_source,
        nucleus::format("program name '{}' is not a bare command token: a program "
                        "name carries only letters, digits, '.', '_' and '-' "
                        "(it reaches shell command position unquoted)",
                        escaped_text(prog))});
}

}

#endif

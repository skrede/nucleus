#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_LEXER_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_LEXER_H

#include "nucleus/expected.h"

#include "nucleus/tokenizer/resolve_error.h"

#include <string>
#include <vector>
#include <string_view>

namespace nucleus {

// Lexed shape of a single ${...} token. Field form (${category.name}) has
// is_function == false and an empty args vector; function form
// (${category.name(arg, ...)}) has is_function == true and one entry per
// top-level comma-separated argument. Nested ${...} substrings inside an
// argument are preserved verbatim -- the resolver recurses on each argument
// separately, so the lexer never expands them itself.
struct lexed_token
{
    std::string category;
    std::string name;
    std::vector<std::string> args;
    bool is_function = false;
};

// Lexes a single token spanning the whole input, from the leading `${` to the
// final `}`. Returns parse_error on malformed input: missing brace pair, empty
// category, empty name, a dotted name, unbalanced parens or quotes, or stray
// content after the closing paren.
[[nodiscard]] expected<lexed_token, resolve_error> lex_token(std::string_view token);

}

#endif

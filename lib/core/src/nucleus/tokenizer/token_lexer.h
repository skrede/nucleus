#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_LEXER_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKEN_LEXER_H

#include "nucleus/expected.h"

#include "nucleus/tokenizer/named_args.h"
#include "nucleus/tokenizer/resolve_error.h"

#include <string>
#include <vector>
#include <string_view>

namespace nucleus {

// Lexed shape of a single ${...} token. Field form (${category.name}) has
// is_function == false and an empty args vector; function form
// (${category.name(name=value, ...)}) has is_function == true and one
// token_argument per top-level comma-separated `name=value` pair (a value may be
// a `[ ... ]` list). Nested ${...} substrings inside an argument value are
// preserved verbatim -- the resolver recurses on each value separately, so the
// lexer never expands them itself.
struct lexed_token
{
    std::string category;
    std::string name;
    std::vector<token_argument> args;
    bool is_function = false;
};

// Lexes a single token spanning the whole input, from the leading `${` to the
// final `}`. Returns parse_error on malformed input: missing brace pair, empty
// category, empty name, a dotted name, unbalanced parens or quotes, or stray
// content after the closing paren.
expected<lexed_token, resolve_error> lex_token(std::string_view token);

// Splits body on top-level '??' outside ${} nesting and quotes; returns a
// single-element vector when no top-level '??' is present.
std::vector<std::string_view> split_fallback_arms(std::string_view body);

}

#endif

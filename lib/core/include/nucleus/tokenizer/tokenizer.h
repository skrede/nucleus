#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_H

#include "nucleus/expected.h"

#include "nucleus/tokenizer/resolve_error.h"

#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <functional>
#include <string_view>

namespace nucleus {

// The result of resolving one field or function: a produced string or a
// resolution error. Tokenizers traffic purely in strings -- a token always
// expands to text spliced back into the surrounding value.
using token_result = expected<std::string, resolve_error>;

// A field resolver: ${category.name} with no argument list. The closure
// receives nothing and produces a string-or-error.
using field_resolver = std::function<token_result()>;

// A wildcard field resolver: matches any ${category.<anything>} the named
// fields did not claim. The closure receives the requested field name. Used by
// dynamic categories such as env, where every variable name is valid input
// rather than a fixed enumeration.
using wildcard_field_resolver = std::function<token_result(std::string_view)>;

// A function resolver: ${category.name(arg, ...)}. The closure receives the
// already-resolved argument list (each nested ${...} inside an arg is expanded
// before the call) and produces a string-or-error. Arity policy lives in the
// closure, keeping the registry dispatch free of per-function knowledge.
using function_resolver = std::function<token_result(std::span<const std::string>)>;

struct token_field
{
    std::string name;
    field_resolver resolve;
};

struct token_function
{
    std::string name;
    function_resolver resolve;
};

// A built tokenizer: one category and the field / function / wildcard surface it
// answers for. Immutable once built (no setters); the registry stores it and
// dispatches against it. It is value-copyable -- a copy deep-copies its fields,
// functions, and wildcard closures -- so a sealed config_space's tokenizer
// registry can be deep-copied by expand() with no shared state. The category
// string is the ${category....} head a token must name to reach this tokenizer.
class tokenizer
{
public:
    tokenizer() = default;

    tokenizer(std::string category,
              std::vector<token_field> fields,
              std::vector<token_function> functions,
              wildcard_field_resolver wildcard)
        : m_category(std::move(category))
        , m_fields(std::move(fields))
        , m_functions(std::move(functions))
        , m_wildcard(std::move(wildcard))
    {
    }

    tokenizer(const tokenizer &) = default;
    tokenizer &operator=(const tokenizer &) = default;

    tokenizer(tokenizer &&) noexcept = default;
    tokenizer &operator=(tokenizer &&) noexcept = default;

    [[nodiscard]] std::string_view category() const noexcept { return m_category; }

    [[nodiscard]] token_result resolve_field(std::string_view name) const;
    [[nodiscard]] token_result resolve_function(std::string_view name,
                                                std::span<const std::string> args) const;

private:
    std::string m_category;
    std::vector<token_field> m_fields;
    std::vector<token_function> m_functions;
    wildcard_field_resolver m_wildcard;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_RESOLVE_ERROR_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_RESOLVE_ERROR_H

#include <string>
#include <utility>

namespace nucleus {

// Why a single ${...} resolution failed. The lexer reports malformed syntax;
// the dispatch path reports a miss on a category, field, or function, or an
// arity mismatch; the guards report the two loud halting conditions a recursive
// expansion must never silently swallow -- a cyclic/self reference and a depth
// overflow. These last two are the named errors token expansion raises instead
// of recursing forever.
enum class resolve_errc
{
    parse_error,
    unknown_category,
    missing_field,
    unknown_function,
    arg_count_mismatch,
    out_of_scope_context,
    cyclic_reference,
    depth_exceeded,
    budget_exceeded
};

// A resolution error: a machine-branchable code plus a human-readable message.
// The message carries the loud, named detail -- e.g. the ordered cycle chain
// "a -> b -> a" -- so a host surfaces a precise diagnostic without re-deriving
// it. Construction keeps both halves together so a code and its message never
// drift apart.
struct resolve_error
{
    resolve_errc code;
    std::string message;

    resolve_error(resolve_errc c, std::string text)
        : code(c), message(std::move(text))
    {
    }
};

inline const char *to_string(resolve_errc code) noexcept
{
    switch(code)
    {
    case resolve_errc::parse_error: return "parse_error";
    case resolve_errc::unknown_category: return "unknown_category";
    case resolve_errc::missing_field: return "missing_field";
    case resolve_errc::unknown_function: return "unknown_function";
    case resolve_errc::arg_count_mismatch: return "arg_count_mismatch";
    case resolve_errc::out_of_scope_context: return "out_of_scope_context";
    case resolve_errc::cyclic_reference: return "cyclic_reference";
    case resolve_errc::depth_exceeded: return "depth_exceeded";
    case resolve_errc::budget_exceeded: return "budget_exceeded";
    }
    return "unknown";
}

}

#endif

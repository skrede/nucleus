#include "nucleus/tokenizer/token_lexer.h"

#include <cctype>
#include <cstddef>
#include <utility>

namespace nucleus {

namespace {

unexpected<resolve_error> parse_failure(std::string text)
{
    return unexpected(resolve_error(resolve_errc::parse_error, std::move(text)));
}

expected<std::string_view, resolve_error> strip_braces(std::string_view token)
{
    if(token.size() < 3 || !token.starts_with("${") || !token.ends_with("}"))
        return unexpected(resolve_error(resolve_errc::parse_error,
                                  "token is not enclosed in ${...}"));
    return token.substr(2, token.size() - 3);
}

struct head_parse
{
    std::string_view category;
    std::string_view name;
    std::size_t paren_pos;
};

// Supported nesting shapes (recursive-to-fixpoint, resolved before this lexer
// runs on a flat head):
//
//   - Head-of-field nesting: `${cat.${x}}` -- the inner `${x}` resolves first,
//     producing a flat `${cat.value}` field token that this lexer then parses.
//     The resolver flattens this before calling lex_token (resolver_scope).
//   - Function-argument nesting: `${f.g(${b})}` -- the inner `${b}` lives inside
//     an argument list and is preserved here verbatim, then resolved per argument
//     by the resolver. parse_args keeps it as one literal argument.
//
// NOT supported: a dynamically-named function, where a nested `${...}` forms the
// function NAME ahead of a top-level '(' (e.g. `${cat.${x}(args)}`). The head is
// never pre-flattened in that case, so the nested token would reach this lexer as
// a literal function name. That is rejected here as a clean, named parse error
// rather than dispatched with an unresolved `${...}` name.

// Splits the body head into (category, name) and locates the first '(' (or npos
// for field form). Rejects an empty category, an empty name, a name that still
// carries a dot (a multi-dot head is malformed), and a dynamically-named function
// (a nested ${...} in the function name -- an unsupported nesting shape).
// Colon-scheme heads (abs: / rel:) are recognized before the dot-check; they
// have no function form, so paren_pos is always npos for scheme refs.
expected<head_parse, resolve_error> parse_head(std::string_view body)
{
    auto colon = body.find(':');
    if(colon != std::string_view::npos && colon > 0)
    {
        auto scheme = body.substr(0, colon);
        if(scheme == "abs" || scheme == "rel")
        {
            head_parse out;
            out.category = scheme;
            out.name     = body.substr(colon + 1);
            out.paren_pos = std::string_view::npos;
            if(out.name.empty())
                return parse_failure("scheme ref body is empty after ':'");
            return out;
        }
    }

    auto dot = body.find('.');
    if(dot == std::string_view::npos || dot == 0)
        return unexpected(resolve_error(resolve_errc::parse_error,
                                  "token body is missing a category.name head"));

    head_parse out;
    out.category = body.substr(0, dot);

    auto paren = body.find('(', dot + 1);
    auto name_end = (paren == std::string_view::npos) ? body.size() : paren;
    out.name = body.substr(dot + 1, name_end - dot - 1);
    out.paren_pos = paren;

    if(out.name.empty() || out.name.find('.') != std::string_view::npos)
        return unexpected(resolve_error(resolve_errc::parse_error,
                                  "token name is empty or contains a dot"));
    // A nested ${...} surviving into the name means a dynamically-named function
    // (`${cat.${x}(args)}`): nesting in the head before a top-level '(' is not a
    // supported shape. Reject it loudly rather than dispatch an unresolved name.
    if(out.name.find("${") != std::string_view::npos)
        return unexpected(resolve_error(resolve_errc::parse_error,
                                  "dynamically-named functions are not supported: "
                                  "a nested ${...} may not form a function name"));
    return out;
}

void append_significant(std::string &current, std::size_t &significant_end, char c)
{
    current += c;
    significant_end = current.size();
}

// Splits the argument list between the open paren and the matching close paren
// on top-level commas. Honors paren depth, brace depth (so a nested ${...} arg
// stays one literal argument), and quote state. A surrounding pair of matching
// quotes on a whole argument is stripped. Leading/trailing whitespace around an
// argument boundary is trimmed; interior whitespace survives.
expected<std::vector<std::string>, resolve_error> parse_args(std::string_view body, std::size_t open_paren)
{
    std::vector<std::string> args;
    std::string current;
    std::size_t significant_end = 0;
    int paren_depth = 0;
    int brace_depth = 0;
    char quote_char = '\0';
    bool current_quoted = false;

    auto push_arg = [&] {
        current.resize(significant_end);
        args.push_back(std::move(current));
        current.clear();
        significant_end = 0;
        current_quoted = false;
    };

    for(std::size_t i = open_paren + 1; i < body.size(); ++i)
    {
        char c = body[i];

        if(quote_char != '\0')
        {
            if(c == quote_char) { quote_char = '\0'; continue; }
            append_significant(current, significant_end, c);
            continue;
        }

        if(c == '\'' || c == '"')
        {
            if(current.empty() && paren_depth == 0 && brace_depth == 0)
                current_quoted = true;
            quote_char = c;
            significant_end = current.size();
            continue;
        }

        if(c == '$' && i + 1 < body.size() && body[i + 1] == '{')
        {
            ++brace_depth;
            append_significant(current, significant_end, c);
            append_significant(current, significant_end, body[++i]);
            continue;
        }
        if(c == '}' && brace_depth > 0)
        {
            --brace_depth;
            append_significant(current, significant_end, c);
            continue;
        }

        if(brace_depth == 0)
        {
            if(c == '(') { ++paren_depth; append_significant(current, significant_end, c); continue; }
            if(c == ')')
            {
                if(paren_depth == 0)
                {
                    if(i + 1 != body.size())
                        return parse_failure("stray content after token argument list");
                    if(!(current.empty() && args.empty() && !current_quoted))
                        push_arg();
                    return args;
                }
                --paren_depth;
                append_significant(current, significant_end, c);
                continue;
            }
            if(c == ',' && paren_depth == 0) { push_arg(); continue; }
        }

        if(c == ' ' || c == '\t')
        {
            if(!current.empty())
                current += c;
            continue;
        }
        append_significant(current, significant_end, c);
    }
    return parse_failure("token argument list is not closed");
}

}

expected<lexed_token, resolve_error> lex_token(std::string_view token)
{
    auto body = strip_braces(token);
    if(!body) return unexpected(std::move(body).error());

    auto head = parse_head(body.value());
    if(!head) return unexpected(std::move(head).error());

    lexed_token out;
    out.category = std::string(head.value().category);
    out.name = std::string(head.value().name);

    if(head.value().paren_pos == std::string_view::npos)
        return out;

    out.is_function = true;
    auto args = parse_args(body.value(), head.value().paren_pos);
    if(!args) return unexpected(std::move(args).error());
    out.args = std::move(args).value();
    return out;
}

}

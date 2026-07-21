#include "nucleus/tokenizer/token_lexer.h"

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
    std::size_t paren_pos{};
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

void skip_ws(std::string_view body, std::size_t &i)
{
    while(i < body.size() && (body[i] == ' ' || body[i] == '\t'))
        ++i;
}

// Reads one scalar value starting at i, stopping (without consuming) at a
// top-level character in `stops`. Single/double quotes group and protect a
// literal (stripped, content verbatim, so '' is the empty string); a nested
// ${...} is copied verbatim at brace depth; a balanced (...) run survives. Bare
// leading/trailing whitespace is trimmed; interior and quoted whitespace survive.
expected<std::string, resolve_error> read_scalar(std::string_view body, std::size_t &i,
                                                 std::string_view stops)
{
    std::string current;
    std::size_t significant_end = 0;
    int brace_depth = 0;
    int paren_depth = 0;
    char quote_char = '\0';
    auto keep = [&](char c) { current += c; significant_end = current.size(); };

    for(; i < body.size(); ++i)
    {
        char const c = body[i];
        if(quote_char != '\0')
        {
            if(c == quote_char) { quote_char = '\0'; continue; }
            keep(c);
            continue;
        }
        if(c == '\'' || c == '"') { quote_char = c; continue; }
        if(c == '$' && i + 1 < body.size() && body[i + 1] == '{')
        {
            ++brace_depth;
            keep(c);
            keep(body[++i]);
            continue;
        }
        if(c == '}' && brace_depth > 0) { --brace_depth; keep(c); continue; }
        if(brace_depth == 0)
        {
            if(c == '(') { ++paren_depth; keep(c); continue; }
            if(c == ')' && paren_depth > 0) { --paren_depth; keep(c); continue; }
            if(paren_depth == 0 && stops.find(c) != std::string_view::npos)
                break;
        }
        if(c == ' ' || c == '\t')
        {
            if(!current.empty())
                current += c;
            continue;
        }
        keep(c);
    }
    if(quote_char != '\0')
        return parse_failure("unterminated quote in token argument value");
    current.resize(significant_end);
    return current;
}

// Reads the argument name up to the top-level '='. Trims surrounding whitespace;
// rejects an empty name or a missing '='.
expected<std::string, resolve_error> read_arg_name(std::string_view body, std::size_t &i)
{
    std::size_t const start = i;
    while(i < body.size() && body[i] != '=' && body[i] != ',' && body[i] != ')')
        ++i;
    if(i >= body.size() || body[i] != '=')
        return parse_failure("expected name=value in token argument list");
    std::string_view const raw = body.substr(start, i - start);
    ++i;  // consume '='
    auto first = raw.find_first_not_of(" \t");
    if(first == std::string_view::npos)
        return parse_failure("empty argument name in token argument list");
    auto last = raw.find_last_not_of(" \t");
    return std::string(raw.substr(first, last - first + 1));
}

// Reads one `name = value` argument. A value beginning with '[' is a list whose
// elements are scalars; otherwise it is a single scalar. A '[' is a list opener
// only as the first non-space character of a value -- elsewhere it is literal.
expected<token_argument, resolve_error> read_arg(std::string_view body, std::size_t &i)
{
    auto name = read_arg_name(body, i);
    if(!name) return unexpected(std::move(name).error());

    token_argument arg;
    arg.name = std::move(name).value();
    skip_ws(body, i);

    if(i < body.size() && body[i] == '[')
    {
        arg.is_list = true;
        ++i;  // consume '['
        skip_ws(body, i);
        if(i < body.size() && body[i] == ']') { ++i; return arg; }
        for(;;)
        {
            auto element = read_scalar(body, i, ",]");
            if(!element) return unexpected(std::move(element).error());
            arg.values.push_back(std::move(element).value());
            skip_ws(body, i);
            if(i >= body.size())
                return parse_failure("token argument list is not closed");
            if(body[i] == ',') { ++i; continue; }
            if(body[i] == ']') { ++i; return arg; }
            return parse_failure("expected ',' or ']' in list argument value");
        }
    }

    auto value = read_scalar(body, i, ",)");
    if(!value) return unexpected(std::move(value).error());
    arg.values.push_back(std::move(value).value());
    return arg;
}

// Parses the named argument list between the open paren and the matching close
// paren: comma-separated `name=value` pairs (a value may be a `[ ... ]` list).
// An empty list `()` yields no arguments. Positional arguments are no longer a
// valid form -- a value with no `name=` is a parse error.
expected<std::vector<token_argument>, resolve_error> parse_args(std::string_view body,
                                                                std::size_t open_paren)
{
    std::vector<token_argument> args;
    std::size_t i = open_paren + 1;
    skip_ws(body, i);
    if(i < body.size() && body[i] == ')')
    {
        ++i;
        if(i != body.size())
            return parse_failure("stray content after token argument list");
        return args;
    }
    for(;;)
    {
        auto arg = read_arg(body, i);
        if(!arg) return unexpected(std::move(arg).error());
        for(const auto &seen : args)
            if(seen.name == arg.value().name)
                return parse_failure("duplicate argument '" + arg.value().name
                                     + "' in token call");
        args.push_back(std::move(arg).value());
        skip_ws(body, i);
        if(i >= body.size())
            return parse_failure("token argument list is not closed");
        if(body[i] == ',') { ++i; continue; }
        if(body[i] == ')')
        {
            ++i;
            if(i != body.size())
                return parse_failure("stray content after token argument list");
            return args;
        }
        return parse_failure("expected ',' or ')' in token argument list");
    }
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

std::vector<std::string_view> split_fallback_arms(std::string_view body)
{
    auto trim = [](std::string_view s) -> std::string_view {
        auto first = s.find_first_not_of(" \t");
        if(first == std::string_view::npos)
            return {};
        auto last = s.find_last_not_of(" \t");
        return s.substr(first, last - first + 1);
    };

    std::vector<std::string_view> arms;
    int brace_depth = 0;
    bool in_quote = false;
    std::size_t arm_start = 0;

    for(std::size_t i = 0; i < body.size(); ++i)
    {
        char const c = body[i];

        if(in_quote)
        {
            if(c == '"')
                in_quote = false;
            continue;
        }

        if(c == '"')
        {
            in_quote = true;
            continue;
        }

        if(c == '$' && i + 1 < body.size() && body[i + 1] == '{')
        {
            ++brace_depth;
            ++i;
            continue;
        }

        if(c == '}' && brace_depth > 0)
        {
            --brace_depth;
            continue;
        }

        if(brace_depth == 0 && c == '?' && i + 1 < body.size() && body[i + 1] == '?')
        {
            arms.push_back(trim(body.substr(arm_start, i - arm_start)));
            i += 2;
            arm_start = i;
            --i;  // loop increment brings i back to arm_start
        }
    }

    arms.push_back(trim(body.substr(arm_start)));
    return arms;
}

}

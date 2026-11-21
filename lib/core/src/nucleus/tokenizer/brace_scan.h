#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_BRACE_SCAN_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_BRACE_SCAN_H

#include "nucleus/tokenizer/resolve_error.h"

#include "nucleus/format.h"

#include <vector>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nucleus {

// True when c was consumed as quote state; quote_char carries the open delimiter,
// or '\0' outside a quoted run.
inline bool track_quote(char c, char &quote_char)
{
    if(quote_char != '\0')
    {
        if(c == quote_char)
            quote_char = '\0';
        return true;
    }
    if(c == '\'' || c == '"')
    {
        quote_char = c;
        return true;
    }
    return false;
}

// The index of the '}' closing a token opened immediately before body_start, or
// npos when the token never closes. A brace inside a quoted run is literal text
// -- a function argument or a fallback arm may carry one -- so it neither opens
// nor closes a token.
inline std::size_t scan_braced_span(std::string_view text, std::size_t body_start)
{
    std::int32_t brace_depth = 1;
    char         quote_char  = '\0';
    for(std::size_t i = body_start; i < text.size(); ++i)
    {
        char const c = text[i];
        if(track_quote(c, quote_char))
            continue;
        if(c == '$' && i + 1 < text.size() && text[i + 1] == '{')
        {
            ++brace_depth;
            ++i;
            continue;
        }
        if(c == '}' && --brace_depth == 0)
            return i;
    }
    return std::string_view::npos;
}

// One past the quoted run that opens at at, at itself when no run opens there,
// or npos when the run never closes.
inline std::size_t skip_quoted(std::string_view text, std::size_t at)
{
    char quote_char{};
    if(at >= text.size() || !track_quote(text[at], quote_char))
        return at;
    std::size_t i = at + 1;
    while(quote_char != '\0')
    {
        if(i == text.size())
            return std::string_view::npos;
        track_quote(text[i++], quote_char);
    }
    return i;
}

// One past the quoted run or nested ${...} that opens at at, at itself when neither
// opens there, or npos when it never closes.
inline std::size_t skip_nested(std::string_view text, std::size_t at)
{
    if(!text.substr(at).starts_with("${"))
        return skip_quoted(text, at);
    std::size_t const close = scan_braced_span(text, at + 2);
    return close == std::string_view::npos ? close : close + 1;
}

// The first index at or after from where needle starts outside every quoted run and
// every nested ${...}, or npos, also when a run or nested token opened before it never
// closes. A nested token's own "${" is top-level text.
inline std::size_t find_top_level(std::string_view text, std::string_view needle, std::size_t from = 0)
{
    std::size_t i = from;
    while(i < text.size())
    {
        if(text.substr(i).starts_with(needle))
            return i;
        if(std::size_t const past = skip_nested(text, i); past != i)
            i = past;
        else
            ++i;
    }
    return std::string_view::npos;
}

// One past the ')' closing the '(' at open, stepping over quoted runs and nested ${...}
// without looking inside them, or npos when the list never closes.
inline std::size_t skip_argument_list(std::string_view text, std::size_t open)
{
    std::int32_t depth = 0;
    for(std::size_t i = open; i < text.size();)
    {
        if(std::size_t const past = skip_nested(text, i); past != i)
        {
            i = past;
            continue;
        }
        if(text[i] == '(')
            ++depth;
        else if(text[i] == ')' && --depth == 0)
            return i + 1;
        ++i;
    }
    return std::string_view::npos;
}

// The first '??' at or after from outside every quoted run, nested ${...} and argument
// list, or npos, also when one of those opened before it never closes.
inline std::size_t find_fallback_operator(std::string_view body, std::size_t from = 0)
{
    while(from != std::string_view::npos)
    {
        std::size_t const op    = find_top_level(body, "??", from);
        std::size_t const paren = find_top_level(body, "(", from);
        if(paren == std::string_view::npos || op < paren)
            return op;
        from = skip_argument_list(body, paren);
    }
    return std::string_view::npos;
}

// The index of the quote opening the run still open at the end of text, scanning from
// body_start, or npos when every run closes.
inline std::size_t open_quote_at_end(std::string_view text, std::size_t body_start)
{
    char        quote_char{};
    std::size_t opened = std::string_view::npos;
    for(std::size_t i = body_start; i < text.size(); ++i)
    {
        char const before = quote_char;
        track_quote(text[i], quote_char);
        if(before != quote_char && quote_char != '\0')
            opened = i;
    }
    return quote_char != '\0' ? opened : std::string_view::npos;
}

// The parse error for a token opened just before body_start that never closes; it names
// the quote still open at the end of text, whose run swallowed the closing brace.
inline resolve_error unterminated_token_error(std::string_view text, std::size_t body_start)
{
    std::size_t const quote = open_quote_at_end(text, body_start);
    if(quote == std::string_view::npos)
        return resolve_error(resolve_errc::parse_error, "unterminated ${ in value");
    return resolve_error(resolve_errc::parse_error,
                         nucleus::format("unterminated ${{ in value: {} at offset {} is never closed", text[quote], quote));
}

// Splits body on each '??' find_fallback_operator finds, so a '??' inside an argument
// list does not split, trimming blanks from every arm; a body with none gives one arm.
inline std::vector<std::string_view> split_fallback_arms(std::string_view body)
{
    auto trim = [](std::string_view s) -> std::string_view
    {
        auto const first = s.find_first_not_of(" \t");
        if(first == std::string_view::npos)
            return {};
        return s.substr(first, s.find_last_not_of(" \t") - first + 1);
    };
    std::vector<std::string_view> arms;
    std::size_t                   start = 0;
    std::size_t                   at    = find_fallback_operator(body);
    while(at != std::string_view::npos)
    {
        arms.push_back(trim(body.substr(start, at - start)));
        start = at + 2;
        at    = find_fallback_operator(body, start);
    }
    arms.push_back(trim(body.substr(start)));
    return arms;
}

}

#endif

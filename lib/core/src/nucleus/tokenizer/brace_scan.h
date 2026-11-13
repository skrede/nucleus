#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_BRACE_SCAN_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_BRACE_SCAN_H

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

// The first index at or after from where needle starts outside every quoted run and
// every nested ${...}, or npos, also when a run or nested token opened before it never
// closes. A nested token's own "${" is top-level text.
inline std::size_t find_top_level(std::string_view text, std::string_view needle, std::size_t from = 0)
{
    std::size_t i = from;
    while(i < text.size())
    {
        std::string_view const rest = text.substr(i);
        if(rest.starts_with(needle))
            return i;
        if(rest.starts_with("${"))
        {
            i = scan_braced_span(text, i + 2);
            if(i == std::string_view::npos)
                return i;
            ++i;
        }
        else if(std::size_t const past = skip_quoted(text, i); past != i)
            i = past;
        else
            ++i;
    }
    return std::string_view::npos;
}

// Splits body on each top-level '??', trimming blanks from every arm; a body with
// no top-level '??' gives one arm.
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
    std::size_t                   at    = find_top_level(body, "??");
    while(at != std::string_view::npos)
    {
        arms.push_back(trim(body.substr(start, at - start)));
        start = at + 2;
        at    = find_top_level(body, "??", start);
    }
    arms.push_back(trim(body.substr(start)));
    return arms;
}

}

#endif

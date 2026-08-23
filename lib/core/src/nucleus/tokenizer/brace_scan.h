#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_BRACE_SCAN_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_BRACE_SCAN_H

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

}

#endif

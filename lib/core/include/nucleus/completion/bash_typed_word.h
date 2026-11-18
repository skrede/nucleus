#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H

#include <string>
#include <string_view>

namespace nucleus::detail {

// Sets `quote` to the quote the typed word leaves open, or to empty (the bare spelling, inert under any quote, though
// readline may then insert other than the candidate) under `$'`, `$"`, or a quote byte or backslash right of the cursor.
// Fails for a word ending inside a quote opened after other text, whose reply would repeat the text readline keeps
// before it, or, with a quote byte or backslash right of the cursor, for any word that opened one after other text.
inline constexpr std::string_view bash_quote_tracker = R"({
    local rest="$1" c seen="" reopened="" ever=""
    quote=""
    while [ -n "$rest" ]; do
        c="${rest%"${rest#?}"}"; rest="${rest#?}"
        case "$quote$c" in
        \\ | \"\\ | \$\'\\ | \$\"\\) rest="${rest#?}" ;;
        \' | \") quote="$c"; reopened="$seen"; ever="$seen" ;;
        \$) case "$rest" in \'* | \"*) quote="\$${rest%"${rest#?}"}"; rest="${rest#?}"; reopened="$seen"; ever="$seen" ;; esac ;;
        \'\' | \"\" | \$\'\' | \$\"\") quote=""; reopened="" ;;
        esac
        seen=1
    done
    local i=0 at=0 gap word="${COMP_WORDS[COMP_CWORD]}"
    while :; do
        gap="${COMP_LINE:at}"; gap="${gap%%[![:space:]]*}"; at=$((at + ${#gap}))
        [ "$i" -lt "$COMP_CWORD" ] || break
        at=$((at + ${#COMP_WORDS[i]})); i=$((i + 1))
    done
    at=$((COMP_POINT - at)); [ "$at" -ge 0 ] || at=0; [ -n "$COMP_LINE" ] || at=${#word}
    case "${word:at}" in *\'* | *\"* | *\\*) quote=""; [ -z "$ever" ]; return ;; esac
    case "$quote" in \$*) quote="" ;; esac
    [ -z "$reopened" ]
}
)";

inline std::string bash_quote_function(const std::string &ident)
{
    return "_" + ident + "_quote()\n" + std::string(bash_quote_tracker);
}

}

#endif

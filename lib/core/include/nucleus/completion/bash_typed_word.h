#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H

#include <string>
#include <string_view>

namespace nucleus::detail {

// The tracker spells a reply for the quote the typed word leaves open; with a quote byte or backslash right of the
// cursor, or under `$'` and `$"`, which report no quote, it takes the bare spelling, inert under any quote, though the
// value readline then inserts is not always the candidate. Its status is non-zero where a reply would repeat the text
// readline keeps before a quote opened after other text, for a word ending inside one; with a quote byte or backslash
// right of the cursor it cannot tell which side of the cursor the quote opened on, so it refuses any word that opened
// one after other text, even where readline keeps nothing.
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
    case "${COMP_LINE:COMP_POINT}" in *\'* | *\"* | *\\*) quote=""; [ -z "$ever" ]; return ;; esac
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

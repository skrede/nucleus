#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H

#include <string>
#include <string_view>

namespace nucleus::detail {

// The tracker spells a reply for the quote bash's quoting of the typed word leaves open, not for its first byte; that
// word runs past the cursor, so a quote byte or backslash right of the cursor takes the bare spelling, inert under any
// quote. `$'` and `$"` report no quote and so take the bare spelling, inert there though not always the typed value.
// Its status is non-zero for a word ending inside a quote opened after another closed, because readline keeps the text
// before that quote and any reply would repeat it.
inline constexpr std::string_view bash_quote_tracker = R"({
    local rest="$1" c closed="" reopened=""
    quote=""
    case "${COMP_LINE:COMP_POINT}" in *\'* | *\"* | *\\*) return 0 ;; esac
    while [ -n "$rest" ]; do
        c="${rest%"${rest#?}"}"; rest="${rest#?}"
        case "$quote$c" in
        \\ | \"\\ | \$\'\\ | \$\"\\) rest="${rest#?}" ;;
        \' | \") quote="$c"; reopened="$closed" ;;
        \$) case "$rest" in \'* | \"*) quote="\$${rest%"${rest#?}"}"; rest="${rest#?}"; reopened="$closed" ;; esac ;;
        \'\' | \"\" | \$\'\' | \$\"\") quote=""; closed=1; reopened="" ;;
        esac
    done
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

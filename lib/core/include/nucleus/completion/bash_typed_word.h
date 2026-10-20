#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_TYPED_WORD_H

#include "nucleus/completion/completion_model.h"

#include <string>
#include <string_view>

namespace nucleus::detail {

// readline inserts a reply under the quote open at the cursor, which bash's own quoting of the whole typed word
// decides, so the tracker follows that quoting rather than the word's first byte. `$'` and `$"` report no quote
// and so take the bare spelling, inert there though not always the typed value. `kept` is what readline keeps after
// a quote the word closed and reopened, so the next reply can drop it.
inline constexpr std::string_view bash_quote_tracker = R"({
    local rest="$1" c i=0 at=0
    quote=""; kept=""
    while [ -n "$rest" ]; do
        c="${rest%"${rest#?}"}"; rest="${rest#?}"; i=$((i + 1))
        case "$quote$c" in
        \\ | \"\\ | \$\'\\ | \$\"\\) rest="${rest#?}"; i=$((i + 1)) ;;
        \' | \") quote="$c"; at=$i ;;
        \$) case "$rest" in \'* | \"*) quote="\$${rest%"${rest#?}"}"; rest="${rest#?}"; i=$((i + 1)) ;; esac ;;
        \'\' | \"\" | \$\'\' | \$\"\") quote="" ;;
        esac
    done
    case "$quote" in \$*) quote="" ;; esac
    case "$1" in "$quote"*) [ -n "$quote" ] && [ "$at" -gt 1 ] && kept="${1:1:$((at - 1))}" ;; esac
}
)";

// readline keeps the typed text through its last unescaped word-break byte (up to it for `@`, a prefix readline
// leaves in the text it replaces) and replaces the rest, so each reply drops as many of its own unescaped break
// bytes as the typed value holds. Counting, rather than matching text, is what lets a prefix typed in another
// spelling still be dropped.
inline constexpr std::string_view bash_trim_body = R"({
    local i=${#COMPREPLY[@]} k=0 rest="$1" c r n
    if [ -n "$2" ]; then
        while [ "$i" -gt 0 ]; do i=$((i - 1)); COMPREPLY[i]="${COMPREPLY[i]#"$2"}"; done
        return 0
    fi
    case "$1" in *[\'\"]*) return 0 ;; esac
    while [ -n "$rest" ]; do
        c="${rest%"${rest#?}"}"; rest="${rest#?}"
        case "$c" in
        \\) rest="${rest#?}" ;;
        *) case "$COMP_WORDBREAKS" in *"$c"*) k=$((k + 1)) ;; esac ;;
        esac
    done
    while [ "$k" -gt 0 ] && [ "$i" -gt 0 ]; do
        i=$((i - 1)); r="${COMPREPLY[i]}"; n=0
        while [ "$n" -lt "$k" ] && [ -n "$r" ]; do
            c="${r%"${r#?}"}"; r="${r#?}"
            case "$c" in
            \\) r="${r#?}" ;;
            *) case "$COMP_WORDBREAKS" in *"$c"*) n=$((n + 1)) ;; esac ;;
            esac
        done
        [ "$n" -eq "$k" ] || continue
        case "$c" in @) r="@$r" ;; esac
        COMPREPLY[i]="$r"
    done
}
)";

inline std::string bash_trim_function(const std::string &ident)
{
    return "_" + ident + "_trim()\n" + std::string(bash_trim_body);
}

// `:`, `@` and `=` are the default COMP_WORDBREAKS bytes the line form leaves bare, and `'` and `!` are the bytes
// whose spelling closes and reopens a quote; a value holding none of them leaves readline nothing to keep.
inline bool bash_needs_trim(const completion_option &option)
{
    for(const std::string &value : option.values)
    {
        if(value.find_first_of("'!:@=") != std::string::npos)
            return true;
    }
    return false;
}

inline std::string bash_quote_function(const std::string &ident)
{
    return "_" + ident + "_quote()\n" + std::string(bash_quote_tracker);
}

}

#endif

#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_ORDINAL_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_ORDINAL_H

#include "nucleus/completion/zsh_spec.h"
#include "nucleus/completion/completion_model.h"

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace nucleus::detail {

// `_arguments` matches option names literally, so the walker builds a spec for the word being
// completed: each raw piece is only compared with the typed word, as a quoted pattern, and the
// spec is the escaped pieces, the typed digit runs (`\*` for a slot not yet reached) and the static
// spec's own tail.
inline constexpr std::string_view zsh_ordinal_walker = R"({
    local rest=$1 tail=$2 name= digits
    local -i filled=0
    shift 2
    while (( $# )); do
        if [[ $rest == "$1"* ]]; then
            rest=${rest#"$1"}
        elif [[ $1 == "$rest"* ]]; then
            rest=
        else
            return 1
        fi
        name+=$2
        shift 2
        (( $# )) || break
        digits=${rest%%[!0123456789]*}
        if [[ -n $digits ]]; then
            name+=$digits; rest=${rest#"$digits"}; filled+=1
        elif [[ -z $rest ]]; then
            name+='\*'
        else
            return 1
        fi
    done
    [[ -z $rest ]] && (( filled )) && ordinal_specs+=( "$name$tail" )
}
)";

inline std::string zsh_ordinal_function(const std::string &ident)
{
    return "_" + ident + "_ordinal()\n" + std::string(zsh_ordinal_walker);
}

inline std::string zsh_ordinal_call(const std::string &ident, const completion_option &option)
{
    std::string out = "    _" + ident + "_ordinal \"$ordinal_word\" " + zsh_single_quote(zsh_spec_tail(option));

    const std::vector<std::string> raw     = ordinal_pieces(option);
    const std::vector<std::string> escaped = zsh_name_pieces(option);
    for(std::size_t i = 0; i < raw.size(); ++i)
        out += " " + zsh_single_quote(raw[i]) + " " + zsh_single_quote(escaped[i]);
    return out + "\n";
}

inline std::string zsh_ordinal_calls(const std::string &ident, const completion_model &model)
{
    std::string out = "    local -a ordinal_specs\n    local ordinal_word=${PREFIX%%=*}\n";
    for(const completion_option &option : model.options)
    {
        if(!option.ordinal_slots.empty())
            out += zsh_ordinal_call(ident, option);
    }
    return out;
}

}

#endif

#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_ORDINAL_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_ORDINAL_H

#include "nucleus/completion/zsh_spec.h"
#include "nucleus/completion/zsh_safe_ascii.h"
#include "nucleus/completion/completion_model.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

inline constexpr std::string_view zsh_walker_head = R"({
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
)";

// The walker reads the caller's `inserts_guarded` by zsh's dynamic scoping, as it appends to the caller's `ordinal_specs`.
inline constexpr std::string_view zsh_walker_tail = R"(            name+=$digits; rest=${rest#"$digits"}; filled+=1
        elif [[ -z $rest ]] && (( inserts_guarded )); then
            name+='\*'
        else
            return 1
        fi
    done
    [[ -z $rest ]] && (( filled )) && ordinal_specs+=( "$name$tail" )
}
)";

// `_arguments` matches option names literally, so the walker builds a spec for the word being
// completed: each raw piece is only compared, as a quoted pattern, with the typed word once its
// quoting is removed, and the spec is the escaped pieces, the typed digit runs (from zsh 5.9, `\*`
// for a slot not yet reached) and the static spec's own tail. A digit run fills a slot only inside
// the CLI's ordinal domain, read in base 10 whatever `octalzeroes` says, so the walker accepts
// exactly the flags the CLI does.
inline std::string zsh_ordinal_walker()
{
    const std::string bound = std::to_string(key_path::max_ordinal);
    const std::string check = "(( ${#digits} <= " + std::to_string(bound.size()) + " )) && (( 10#$digits <= " + bound + " ))";
    return std::string(zsh_walker_head) + "            " + check + " || return 1\n" + std::string(zsh_walker_tail);
}

// No program's function name holds a `-`, so the walker cannot take another program's name.
inline std::string zsh_walker_name(const std::string &function)
{
    return function + "-ordinal";
}

inline std::string zsh_ordinal_function(const std::string &function)
{
    return zsh_walker_name(function) + "()\n" + zsh_ordinal_walker();
}

inline constexpr std::string_view zsh_ordinal_locals = "    local -a ordinal_specs\n    local ordinal_word=${(Q)${PREFIX%%=*}}\n";

inline std::string zsh_ordinal_call(const std::string &function, const completion_option &option)
{
    std::string out = zsh_walker_name(function) + " \"$ordinal_word\" " + zsh_single_quote(zsh_spec_tail(option));

    const std::vector<std::string> raw     = ordinal_pieces(option);
    const std::vector<std::string> escaped = zsh_name_pieces(option);
    for(std::size_t i = 0; i < raw.size(); ++i)
        out += " " + zsh_single_quote(raw[i]) + " " + zsh_single_quote(escaped[i]);
    return out + "\n";
}

inline bool zsh_walker_outside_safe_ascii(const completion_option &option)
{
    const std::vector<std::string> pieces = ordinal_pieces(option);
    return std::ranges::any_of(pieces, [](const std::string &piece)
                               { return zsh_flag_outside_safe_ascii(piece); });
}

inline std::string zsh_ordinal_calls(const std::string &function, const completion_model &model, bool quoted)
{
    const std::string indent = quoted ? "        " : "    ";
    std::string       out;
    for(const completion_option &option : model.options)
    {
        if(has_ordinal_slots(option) && zsh_walker_outside_safe_ascii(option) == quoted)
            out += indent + zsh_ordinal_call(function, option);
    }
    return out;
}

}

#endif

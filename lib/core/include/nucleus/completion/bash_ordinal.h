#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_ORDINAL_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_ORDINAL_H

#include "nucleus/completion/bash_quoting.h"
#include "nucleus/completion/bash_line_form.h"
#include "nucleus/completion/completion_model.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <string_view>

namespace nucleus::detail {

inline constexpr std::string_view bash_walker_head = R"({
    local rest="$1" name="" digits filled=0
    shift
    while [ $# -gt 0 ]; do
        case "$rest" in
        "$1"*) rest="${rest#"$1"}" ;;
        *) case "$1" in "$rest"*) rest="" ;; *) return 1 ;; esac ;;
        esac
        name="$name$1"
        shift
        [ $# -gt 0 ] || break
        digits="${rest%%[!0123456789]*}"
        if [ -n "$digits" ]; then
)";

inline constexpr std::string_view bash_walker_tail = R"(            name="$name$digits"; rest="${rest#"$digits"}"; filled=$((filled + 1))
        elif [ -z "$rest" ]; then
            name="$name\*"
        else
            return 1
        fi
    done
    [ -z "$rest" ] && [ "$filled" -gt 0 ] || return 1
    ordinal_name="$name"
}
)";

// The walker matches the typed word against an option's literal pieces in line form, never its flag
// text, so a `*` a delimiter carries is never a slot, and it builds the completed word from those
// pieces, the typed digit runs and `\*` alone, reporting it through the caller's `ordinal_name`. A
// digit run fills a slot only inside the CLI's ordinal domain, so the walker accepts exactly the flags
// the CLI does.
inline std::string bash_ordinal_walker()
{
    const std::string bound = std::to_string(key_path::max_ordinal);
    const std::string check = "[ ${#digits} -le " + std::to_string(bound.size()) + " ] && [ \"$digits\" -le " + bound + " ]";
    return std::string(bash_walker_head) + "            " + check + " || return 1\n" + std::string(bash_walker_tail);
}

inline std::string bash_ordinal_call(const std::string &ident, const std::string &word,
                                     const completion_option &option)
{
    std::string out = "_" + ident + "_ordinal \"" + word + "\"";
    for(const std::string &piece : ordinal_pieces(option))
        out += " " + bash_single_quote(bash_line_word(piece));
    return out;
}

inline std::string bash_ordinal_functions(const std::string &ident, const completion_model &model)
{
    std::string out = "_" + ident + "_ordinal()\n" + bash_ordinal_walker();
    out += "_" + ident + "_ordinals()\n{\n    local ordinal_name\n";
    for(const completion_option &option : model.options)
    {
        if(has_ordinal_slots(option))
            out += "    " + bash_ordinal_call(ident, "$1", option) + " && printf '%s\\n' \"$ordinal_name\"\n";
    }
    return out + "}\n";
}

inline std::string bash_ordinal_dispatch(const std::string &ident, const completion_model &model, const std::string &prefix)
{
    std::string out;
    for(const completion_option &option : model.options)
    {
        if(!has_ordinal_slots(option) || option.values.empty())
            continue;
        out += "        if " + bash_ordinal_call(ident, "$flag", option) + " && [ \"$ordinal_name\" = \"$flag\" ]; then\n";
        out += bash_value_reply("            ", option, ident, prefix);
        out += "        fi\n";
    }
    return out;
}

}

#endif

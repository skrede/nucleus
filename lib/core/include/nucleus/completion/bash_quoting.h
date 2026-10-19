#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_QUOTING_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_QUOTING_H

#include "nucleus/completion/completion_model.h"

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

// bash single-quote escaping: close the quote, emit an escaped quote, reopen.
inline std::string bash_single_quote(const std::string &text)
{
    std::string out = "'";
    for(char const c : text)
    {
        if(c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

// Single-quoting the word list holds it to one literal in the parse of the SCRIPT, which is
// not the parse that matters: `compgen -W` EXPANDS every word it produces -- brace, tilde,
// parameter and arithmetic expansion, command substitution and process substitution all run
// at that second, runtime stage. Each flag and value is therefore escaped for that stage here
// at the emitter, never on the word of a validator upstream, and escaped against an ALLOWLIST
// so that no construct anyone has to think of first survives into it.
// Enumerating what a shell does to a word is a guess, and a word list is a code
// channel, so the test runs the other way: a byte survives unescaped only by being in
// the safe set, and every other byte -- control bytes and everything at or above 0x7f
// included -- is backslashed. A backslashed byte is exempt from every expansion
// `compgen -W` performs and its backslash is dropped by quote removal, so a multi-byte
// sequence escaped byte by byte reads back identical. `*` is safe because `compgen -W`
// does no pathname expansion: escaping it is consumed with no effect on the candidate,
// and the ordinal wildcard stays readable in the emitted script. A newline is the one
// byte this encoding cannot carry, since a backslash before it is a line continuation.
inline bool bash_bare_byte(char c)
{
    static constexpr std::string_view safe_punctuation = "-_./=+,:@%*";
    if(static_cast<std::uint8_t>(c) >= 0x7f)
        return false;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || safe_punctuation.find(c) != std::string_view::npos;
}

inline std::string bash_word(const std::string &text)
{
    std::string out;
    for(char const c : text)
    {
        if(!bash_bare_byte(c))
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// readline inserts a reply verbatim, so a reply holds its candidate as the line must spell it;
// `*` is escaped here because the line globs where compgen does not.
inline std::string bash_line_word(const std::string &text)
{
    std::string out;
    for(char const c : text)
    {
        if(static_cast<std::uint8_t>(c) < 0x80 && (c == '*' || !bash_bare_byte(c)))
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

inline std::string bash_join_flags(const std::vector<completion_option> &options)
{
    std::string out;
    for(std::size_t i = 0; i < options.size(); ++i)
    {
        if(i != 0)
            out.push_back(' ');
        out += bash_word(options[i].flag);
    }
    return out;
}

inline std::string bash_join_values(const std::vector<std::string> &values)
{
    std::string out;
    for(std::size_t i = 0; i < values.size(); ++i)
    {
        if(i != 0)
            out.push_back(' ');
        out += bash_word(values[i]);
    }
    return out;
}

inline std::string bash_line_arm(const std::string &raw, std::vector<std::string> &seen)
{
    const std::string line = bash_line_word(raw);
    if(line == raw || std::ranges::find(seen, raw) != seen.end())
        return {};
    seen.push_back(raw);
    return "    " + bash_single_quote(raw) + ") printf '%s\\n' " + bash_single_quote(line) + " ;;\n";
}

inline std::string bash_line_lookup(const std::string &ident, const completion_model &model)
{
    std::vector<std::string> seen;
    std::string              arms;
    for(const completion_option &option : model.options)
    {
        arms += bash_line_arm(option.flag, seen);
        for(const std::string &value : option.values)
            arms += bash_line_arm(value, seen);
    }
    if(arms.empty())
        return {};
    const std::string out = "_" + ident + "_line()\n{\n    case \"$1\" in\n" + arms;
    return out + "    *) printf '%s\\n' \"$1\" ;;\n    esac\n}\n";
}

// `compgen -W` splits its word list on IFS, and the unquoted command substitution that fills
// COMPREPLY splits again on the same characters -- so a value carrying a separator has to
// survive both. Each value backslash-escapes its own separators for the first split, and the
// assignment narrows IFS to a newline for the second while restoring the default inside the
// substitution, where compgen still needs it. That same unquoted substitution also GLOBS what
// it produces, which nothing escaped at the emitter can reach because compgen has already
// consumed the backslash -- so the assignment runs with globbing suppressed and puts the
// caller's setting back. `case $-` reads that setting portably, where `local -` would need
// bash 4.4 -- above the floor this script supports. `also` runs inside the same substitution.
// A non-empty `line` names the lookup each compgen match is piped through to reach its line form.
inline std::string bash_reply_assignment(const std::string &pad, const std::string &words, const std::string &current,
                                         const std::string &line, const std::string &also)
{
    std::string out = pad + "local noglob=0; case $- in *f*) noglob=1 ;; esac; set -f\n";
    out += pad + R"(local IFS=$'\n'; COMPREPLY=( $(IFS=$' \t\n'; compgen -W )" + words + " -- \"" + current + "\"";
    out += line.empty() ? std::string() : " | while IFS= read -r w; do " + line + " \"$w\"; done";
    out += (also.empty() ? std::string() : "; " + also) + ") )\n";
    return out + pad + "if [ \"$noglob\" = 0 ]; then set +f; fi\n";
}

inline std::string bash_value_reply(const std::string &pad, const completion_option &option, const std::string &line)
{
    const std::string words = bash_single_quote(bash_join_values(option.values));
    std::string       out   = bash_reply_assignment(pad, words, "$val", line, {});
    out += pad + "__ltrim_colon_completions \"$cur\" 2>/dev/null\n";
    return out + pad + "return 0\n";
}

}

#endif

#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_QUOTING_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_QUOTING_H

#include "nucleus/completion/completion_model.h"

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
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

// compgen -W splits its word list on IFS and the unquoted substitution filling COMPREPLY splits again, so each value
// escapes its own separators and the assignment narrows IFS to a newline, keeping the default inside the substitution.
// That substitution also globs what it yields, past any escape, so the assignment runs under `set -f`, which the
// `local -` the completion function opens with undoes on return.
inline std::string bash_reply_assignment(const std::string &pad, const std::string &words, const std::string &current,
                                         const std::string &prefix, const std::string &also)
{
    std::string out = pad + "set -f\n";
    out += pad + "local quote; " + prefix + "_quote \"" + current + "\" || return 0\n";
    out += pad + R"(local IFS=$'\n'; COMPREPLY=( $(IFS=$' \t\n'; compgen -W )" + words + " -- \"" + current + "\"";
    out += " | while IFS= read -r w; do " + prefix + R"(_line "$quote" "$w"; done)";
    return out + (also.empty() ? std::string() : "; " + also) + ") )\n";
}

inline std::string bash_value_reply(const std::string &pad, const completion_option &option, const std::string &prefix)
{
    const std::string words = bash_single_quote(bash_join_values(option.values));
    return bash_reply_assignment(pad, words, "$val", prefix, {}) + pad + "return 0\n";
}

}

#endif

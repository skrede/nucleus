#ifndef HPP_GUARD_NUCLEUS_COMPLETION_ZSH_SPEC_H
#define HPP_GUARD_NUCLEUS_COMPLETION_ZSH_SPEC_H

#include "nucleus/completion/zsh_description.h"
#include "nucleus/completion/completion_model.h"

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nucleus::detail {

inline bool zsh_bare_byte(char c)
{
    static constexpr std::string_view safe_punctuation = "-_./+,@%";
    if(static_cast<std::uint8_t>(c) >= 0x7f)
        return false;
    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    return safe_punctuation.find(c) != std::string_view::npos;
}

// Every string the spec carries as a WORD -- a flag at its head, a member of a
// `(...)` value group -- is escaped here against an allowlist rather than a list of
// dangerous characters: a byte survives unescaped only by being in the safe set, and
// every other byte is backslashed. That covers the spec-grammar delimiters, the
// whitespace separating members, and the expansions zsh runs over a word once it reads
// it back out of the spec, without any of them having been thought of first. Each byte
// in the safe set was measured inert under zsh 5.9: `_arguments` evals a value group
// under `extendedglob nullglob`, so `*` is filename generation and a leading `=` is
// EQUALS expansion, and neither is in it. The escape is unconditional; what a
// registration rule refuses is not this file's business, and assuming otherwise is
// what left the word list a code channel.
inline std::string zsh_word(const std::string &text)
{
    std::string out;
    for(char const c : text)
    {
        if(!zsh_bare_byte(c))
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// A `-` or `+` ending a presence flag is spec grammar, not part of the name: `--x-[d]`
// declares `--x` taking an argument in the same word.
inline std::string zsh_presence_word(const std::string &text)
{
    std::string out = zsh_word(text);
    if(out.ends_with('-') || out.ends_with('+'))
        out.insert(out.size() - 1, 1, '\\');
    return out;
}

// zsh single-quote escaping: a single quote cannot appear inside '...', so we
// close, emit an escaped quote, and reopen -- the same idiom every POSIX shell
// accepts.
inline std::string zsh_single_quote(const std::string &text)
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

inline std::vector<std::string> zsh_name_pieces(const completion_option &option)
{
    const std::vector<std::string> raw = ordinal_pieces(option);
    std::vector<std::string>       pieces;
    pieces.reserve(raw.size());
    for(const std::string &piece : raw)
        pieces.push_back(zsh_word(piece));
    if(option.values.empty())
        pieces.back() = zsh_presence_word(raw.back());
    return pieces;
}

inline std::string zsh_spec_tail(const completion_option &option)
{
    std::string description = "[" + zsh_description(option.description) + "]";
    if(option.values.empty())
        return description;
    std::string group;
    for(std::size_t i = 0; i < option.values.size(); ++i)
    {
        if(i != 0)
            group.push_back(' ');
        group += zsh_word(option.values[i]);
    }
    return "=" + description + ":value:(" + group + ")";
}

inline std::string zsh_spec(const completion_option &option)
{
    const std::vector<std::string> pieces = zsh_name_pieces(option);
    std::string                    spec   = pieces.front();
    for(std::size_t i = 1; i < pieces.size(); ++i)
        spec += "\\*" + pieces[i];
    return spec + zsh_spec_tail(option);
}

}

#endif

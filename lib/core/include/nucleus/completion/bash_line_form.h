#ifndef HPP_GUARD_NUCLEUS_COMPLETION_BASH_LINE_FORM_H
#define HPP_GUARD_NUCLEUS_COMPLETION_BASH_LINE_FORM_H

#include "nucleus/completion/bash_quoting.h"
#include "nucleus/completion/bash_typed_word.h"
#include "nucleus/completion/completion_model.h"

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

using bash_line_spelling = std::string(const std::string &);

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

// readline closes the opening quote after a lone match only when the reply does not already end
// with that byte, so a form ending in its own quote byte gets one more, which closes it.
inline std::string bash_quote_closing(std::string form, char quote)
{
    if(!form.empty() && form.back() == quote)
        form.push_back(quote);
    return form;
}

inline std::string bash_single_quoted_line_word(const std::string &text)
{
    std::string out;
    for(char const c : text)
    {
        if(c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    return bash_quote_closing(out, '\'');
}

// History expansion reads `!` inside double quotes and keeps a backslash before it in the
// argument, so `!` steps out of the quote to be escaped there.
inline std::string bash_double_quoted_line_word(const std::string &text)
{
    static constexpr std::string_view escaped = "$`\"\\";
    std::string                       out;
    for(char const c : text)
    {
        if(c == '!')
            out += R"("\!")";
        else if(escaped.find(c) != std::string_view::npos)
            out += {'\\', c};
        else
            out.push_back(c);
    }
    return bash_quote_closing(out, '"');
}

inline std::string bash_line_arm(const std::string &raw, bash_line_spelling &form, std::vector<std::string> &seen)
{
    const std::string line = form(raw);
    if(line == raw || std::ranges::find(seen, raw) != seen.end())
        return {};
    seen.push_back(raw);
    return "        " + bash_single_quote(raw) + ") printf '%s\\n' " + bash_single_quote(line) + " ;;\n";
}

inline std::string bash_line_arms(const completion_model &model, bash_line_spelling &form)
{
    std::vector<std::string> seen;
    std::string              arms;
    for(const completion_option &option : model.options)
    {
        arms += bash_line_arm(option.flag, form, seen);
        for(const std::string &value : option.values)
            arms += bash_line_arm(value, form, seen);
    }
    return arms;
}

inline std::string bash_line_table(std::string_view typed, const std::string &arms)
{
    const std::string out = "    " + std::string(typed) + ") case \"$2\" in\n" + arms;
    return out + "        *) printf '%s\\n' \"$2\" ;;\n        esac ;;\n";
}

inline std::string bash_line_lookup(const std::string &ident, const completion_model &model)
{
    const std::string single_quoted = bash_line_arms(model, bash_single_quoted_line_word);
    const std::string double_quoted = bash_line_arms(model, bash_double_quoted_line_word);
    const std::string bare          = bash_line_arms(model, bash_line_word);
    std::string       out           = bash_quote_function(ident) + "_" + ident + "_line()\n{\n    case \"$1\" in\n";
    out += bash_line_table("\\'", single_quoted) + bash_line_table("\\\"", double_quoted) + bash_line_table("*", bare);
    return out + "    esac\n}\n";
}

}

#endif

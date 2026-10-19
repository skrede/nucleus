#include "nucleus/completion/completion_generator.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/program_token.h"
#include "nucleus/completion/completion_model.h"
#include "nucleus/completion/completion_projection.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/utility/display_controls.h"

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include <string_view>

namespace nucleus {

namespace {

using help_groups = std::map<std::string, std::vector<std::string>>;

// One plain --help line for a declared element: its flag, then its description,
// its allowed-values list, and a required marker -- each part appended only when
// present. No shell escaping: help text is plain, not a completion spec, but every
// host string is rendered so the text cannot drive the terminal that shows it.
std::string help_line(const std::string &flag, const schema_element &el)
{
    std::string line = "  " + detail::displayed_text(flag);
    if(!el.description.empty())
        line += "  " + detail::displayed_text(el.description);
    if(!el.allowed_values.empty())
    {
        line += " [values: ";
        for(std::size_t i = 0; i < el.allowed_values.size(); ++i)
        {
            if(i != 0)
                line += ", ";
            line += detail::displayed_text(el.allowed_values[i]);
        }
        line += "]";
    }
    if(el.required)
        line += " (required)";
    return line;
}

// Iterates the same projected surface the completions use, so the two never disagree
// on which flags exist; a bare recognized path with no typed element still gets its
// flag line. The map keeps groups in a stable order so the help text is reproducible.
help_groups grouped_help_lines(const schema_registry &schema, const cli_delimiter &delimiter, const key_path &anchor)
{
    std::map<std::string, std::reference_wrapper<const schema_element>> by_path;
    for(const schema_element &el : schema.elements())
        by_path.emplace(el.declared_path().str(), std::cref(el));
    const key_path canonical = detail::canonical_anchor(anchor);
    help_groups    groups;
    for(const key_path &path : schema.surface())
    {
        if(!detail::anchor_admits(path, canonical))
            continue;
        const key_path relative = canonical.empty() ? path : path.relative_to(canonical);
        if(relative.empty())
            continue;
        const std::string flag = flag_of(relative, delimiter);
        const auto        it   = by_path.find(path.str());
        groups[relative.segments().front()].push_back(it != by_path.end() ? help_line(flag, it->second)
                                                                          : "  " + detail::displayed_text(flag));
    }
    return groups;
}

std::string rendered_help(std::string_view prog, const help_groups &groups)
{
    std::string out = detail::displayed_text(prog) + " options:\n";
    for(const auto &[group, lines] : groups)
    {
        out += "\n" + detail::displayed_text(group) + ":\n";
        for(const std::string &line : lines)
            out += line + "\n";
    }
    return out;
}

std::string emitted_script(shell which, const completion_model &model)
{
    // No default label: an added shell must gain a case here rather than fall
    // through silently to the bash emitter.
    std::string script;
    switch(which)
    {
    case shell::zsh:
        script = zsh_emitter{}.emit(model);
        break;
    case shell::bash:
        script = bash_emitter{}.emit(model);
        break;
    }
    return script;
}

}

expected<std::string, error> generate_completion(shell which, const schema_registry &schema,
                                                 std::string_view prog, const cli_delimiter &delimiter,
                                                 const key_path &anchor, std::string_view space_name)
{
    if(auto valid = check_program_token(prog); !valid)
        return unexpected(std::move(valid).error());
    auto space = check_space_name(space_name);
    if(!space)
        return unexpected(std::move(space).error());
    if(auto valid = detail::check_anchor(schema, anchor); !valid)
        return unexpected(std::move(valid).error());
    return emitted_script(which, detail::completion_projection(schema, prog, delimiter, anchor, *space));
}

std::string generate_help(const schema_registry &schema, std::string_view prog,
                          const cli_delimiter &delimiter, const key_path &anchor)
{
    return rendered_help(prog, grouped_help_lines(schema, delimiter, anchor));
}

}

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

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <string_view>

namespace nucleus {

namespace {

// One plain --help line for a declared element: its flag, then its description,
// its allowed-values list, and a required marker -- each part appended only when
// present. No shell escaping: help text is plain, not a completion spec.
std::string help_line(const std::string &flag, const schema_element &el)
{
    std::string line = "  " + flag;
    if(!el.description.empty())
        line += "  " + el.description;
    if(!el.allowed_values.empty())
    {
        line += " [values: ";
        for(std::size_t i = 0; i < el.allowed_values.size(); ++i)
        {
            if(i != 0)
                line += ", ";
            line += el.allowed_values[i];
        }
        line += "]";
    }
    if(el.required)
        line += " (required)";
    return line;
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
    const completion_model model = detail::completion_projection(schema, prog, delimiter, anchor, *space);
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

std::string generate_help(const schema_registry &schema, std::string_view prog,
                          const cli_delimiter &delimiter, const key_path &anchor)
{
    // Iterate the same projected surface the completions use so the two never
    // disagree on which flags exist; join each path to its element for the
    // description/values/required a help line adds. A path registered as a bare
    // recognized target (no typed element) still gets its flag line. Group by the
    // top-level keyspace segment; the map keeps groups in stable alphabetical
    // order so the help text is reproducible.
    std::map<std::string, const schema_element *> by_path;
    for(const schema_element &el : schema.elements())
        by_path.emplace(el.declared_path().str(), &el);

    std::map<std::string, std::vector<std::string>> groups;
    for(const key_path &path : schema.surface())
    {
        if(!anchor.empty()
           && (!path.starts_with(anchor) || path.size() == anchor.size()))
            continue;

        const key_path relative = anchor.empty() ? path : path.relative_to(anchor);
        if(relative.segments().empty())
            continue;
        const std::string &group = relative.segments().front();
        const std::string flag = flag_of(relative, delimiter);
        const auto it = by_path.find(path.str());
        groups[group].push_back(it != by_path.end() ? help_line(flag, *it->second)
                                                     : "  " + flag);
    }

    std::string out = std::string(prog) + " options:\n";
    for(const auto &[group, lines] : groups)
    {
        out += "\n" + group + ":\n";
        for(const std::string &line : lines)
            out += line + "\n";
    }
    return out;
}

}

#include "nucleus/completion/completion_generator.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/program_token.h"
#include "nucleus/completion/completion_model.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/keyspace/key_path.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <string_view>

namespace nucleus {

namespace {

// Index a path's declared value set by its canonical string, so a recognized path
// can pick up the allowed_values of the typed element that declares it. surface()
// includes path-tagged registrations that have no typed element; those carry no
// values and simply complete by name.
std::map<std::string, std::vector<std::string>>
value_sets(const schema_registry &schema)
{
    std::map<std::string, std::vector<std::string>> out;
    for(const schema_element &el : schema.elements())
    {
        if(!el.allowed_values.empty())
            out.emplace(el.declared_path().str(), el.allowed_values);
    }
    return out;
}

// Index a path's description by its canonical string, so a recognized path picks
// up the description of the element that declares it. The description travels the
// SAME keyed path as the allowed_values, keeping both projections in step.
std::map<std::string, std::string>
descriptions(const schema_registry &schema)
{
    std::map<std::string, std::string> out;
    for(const schema_element &el : schema.elements())
    {
        if(!el.description.empty())
            out.emplace(el.declared_path().str(), el.description);
    }
    return out;
}

// Constructs the wildcard flag for a path that crosses `container` at depth
// `container_depth` (number of segments in the container path). The wildcard
// replaces the ordinal position: --prefix-*-suffix under `delimiter`.
std::string wildcard_flag(const key_path &effective,
                                        std::size_t container_depth,
                                        const cli_delimiter &delimiter)
{
    const auto &segs = effective.segments();
    std::string flag = "--";
    for(std::size_t i = 0; i < segs.size(); ++i)
    {
        if(i != 0)
            flag += delimiter.str();
        flag += segs[i];
        if(i + 1 == container_depth)
        {
            flag += delimiter.str();
            flag += "*";
        }
    }
    return flag;
}

// Project the schema's recognized surface into the shell-neutral model. surface()
// is deterministically ordered, so the model -- and every generated script -- is
// reproducible. Each path becomes its canonical flag via the same flag_of() the
// argv surface uses, so completion and the real CLI share one mapping.
// For paths that cross a repeated container, an additional wildcard entry
// is emitted with '*' at the container ordinal position.
// When `space` is non-empty, it is prepended as the leading segment before
// applying flag_of(), so the completion entries match the multispace_argv_source grammar.
completion_model project(const schema_registry &schema, std::string_view prog,
                         const cli_delimiter &delimiter, const key_path &anchor,
                         const key_path &space)
{
    const auto values = value_sets(schema);
    const auto descs = descriptions(schema);
    const std::set<std::string> repeated_containers = schema.repeated_container_paths();

    completion_model model;
    model.prog = std::string(prog);
    for(const key_path &path : schema.surface())
    {
        // Under an anchor only strictly-descendant paths are addressable; they
        // complete by their relative flag. Value sets stay keyed by full path.
        if(!anchor.empty()
           && (!path.starts_with(anchor) || path.size() == anchor.size()))
            continue;

        const key_path relative = anchor.empty() ? path : path.relative_to(anchor);
        const key_path effective = space.empty() ? relative : space.join(relative);

        completion_option opt;
        opt.flag = flag_of(effective, delimiter);
        if(auto it = values.find(path.str()); it != values.end())
            opt.values = it->second;
        if(auto it = descs.find(path.str()); it != descs.end())
            opt.description = it->second;
        const auto option_values = opt.values;
        const auto option_description = opt.description;
        model.options.push_back(std::move(opt));

        std::string prefix;
        for(std::size_t depth = 1; depth < path.size(); ++depth)
        {
            const std::string &seg = path.segments()[depth - 1];
            if(!prefix.empty())
                prefix += key_path::separator;
            prefix += seg;
            if(repeated_containers.contains(prefix) && depth < path.size())
            {
                // depth counts full-path segments; the flag counts effective-path
                // segments, which drop the anchor prefix and gain the space name.
                const std::size_t anchor_offset   = anchor.empty() ? 0 : anchor.size();
                const std::size_t space_offset    = space.size();
                const std::size_t effective_depth = depth - anchor_offset + space_offset;

                completion_option wild;
                wild.flag = wildcard_flag(effective, effective_depth, delimiter);
                wild.has_ordinal_wildcard = true;
                if(!option_values.empty())
                    wild.values = option_values;
                if(!option_description.empty())
                    wild.description = option_description;
                model.options.push_back(std::move(wild));
                break; // one wildcard entry per crossing
            }
        }
    }
    return model;
}

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
    const completion_model model = project(schema, prog, delimiter, anchor, *space);
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

#include "nucleus/completion/completion_generator.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/zsh_emitter.h"
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
[[nodiscard]] std::map<std::string, std::vector<std::string>>
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

// Constructs the wildcard flag for a path that crosses `container` at depth
// `container_depth` (number of segments in the container path). The wildcard
// replaces the ordinal position: --prefix-*-suffix under `delimiter`.
[[nodiscard]] std::string wildcard_flag(const key_path &effective,
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
        // After the last segment of the repeated container, insert the wildcard.
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
// For paths that cross a repeated container (D-12), an additional wildcard entry
// is emitted with '*' at the container ordinal position.
// When space_name is non-empty, it is prepended as the leading segment before
// applying flag_of(), so the completion entries match the multispace_argv_source grammar.
[[nodiscard]] completion_model project(const schema_registry &schema,
                                       std::string_view prog,
                                       const cli_delimiter &delimiter,
                                       const key_path &anchor,
                                       std::string_view space_name)
{
    const auto values = value_sets(schema);
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
        const key_path effective = space_name.empty() ? relative
            : key_path::parse(std::string(space_name) + "/" + relative.str()).value();

        completion_option opt;
        opt.flag = flag_of(effective, delimiter);
        if(auto it = values.find(path.str()); it != values.end())
            opt.values = it->second;
        model.options.push_back(std::move(opt));

        // D-12: emit an additional wildcard entry when the path crosses a repeated
        // container. Walk the full (pre-anchor) path to find the crossing container.
        std::string prefix;
        for(std::size_t depth = 1; depth < path.size(); ++depth)
        {
            const std::string &seg = path.segments()[depth - 1];
            prefix = prefix.empty() ? seg : prefix + key_path::separator + seg;
            if(repeated_containers.count(prefix) && depth < path.size())
            {
                // Compute the container depth in the effective path (accounting for
                // anchor stripping and space_name prepend).
                const std::size_t anchor_offset = anchor.empty() ? 0 : anchor.size();
                const std::size_t space_offset  = space_name.empty() ? 0 : 1;
                const std::size_t effective_depth =
                    depth - anchor_offset + space_offset;

                completion_option wild;
                wild.flag = wildcard_flag(effective, effective_depth, delimiter);
                wild.has_ordinal_wildcard = true;
                if(!opt.values.empty())
                    wild.values = opt.values;
                model.options.push_back(std::move(wild));
                break; // one wildcard entry per crossing
            }
        }
    }
    return model;
}

}

std::string generate_completion(shell which, const schema_registry &schema,
                                std::string_view prog, const cli_delimiter &delimiter,
                                const key_path &anchor, std::string_view space_name)
{
    const completion_model model = project(schema, prog, delimiter, anchor, space_name);
    switch(which)
    {
    case shell::zsh:
        return zsh_emitter{}.emit(model);
    case shell::bash:
    default:
        return bash_emitter{}.emit(model);
    }
}

}

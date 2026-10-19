#ifndef HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_PROJECTION_H
#define HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_PROJECTION_H

#include "nucleus/completion/completion_model.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/utility/escaped_text.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

// Index a path's declared value set by its canonical string, so a recognized path
// can pick up the allowed_values of the typed element that declares it. surface()
// includes path-tagged registrations that have no typed element; those carry no
// values and simply complete by name.
inline std::map<std::string, std::vector<std::string>>
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
inline std::map<std::string, std::string>
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

// Read as the flat argv renderer reads an anchor: each segment by its base name, so a
// concrete anchor selects the same canonical subtree its canonical form does.
inline key_path canonical_anchor(const key_path &anchor)
{
    std::vector<std::string> segments;
    for(const std::string &segment : anchor.segments())
        segments.emplace_back(key_path::base_name(segment));
    return key_path(std::move(segments));
}

inline bool anchor_admits(const key_path &path, const key_path &canonical)
{
    return canonical.empty() || (path.starts_with(canonical) && path.size() > canonical.size());
}

// An ordinal on a segment that names no repeated container selects no instance, so it is
// refused rather than read canonically into a surface the argv source cannot address.
inline expected<void, error> check_anchor(const schema_registry &schema, const key_path &anchor)
{
    const std::set<std::string> containers = schema.repeated_container_paths();
    std::string                 prefix;
    for(const std::string &segment : anchor.segments())
    {
        if(!prefix.empty())
            prefix += key_path::separator;
        prefix += key_path::base_name(segment);
        if(key_path::is_indexed_segment(segment) && !containers.contains(prefix))
            return unexpected(error{errc::malformed_source,
                                    nucleus::format("completion anchor '{}' carries an ordinal on '{}', which is not a "
                                                    "repeated container",
                                                    escaped_text(anchor.str()), escaped_text(prefix))});
    }
    return {};
}

inline std::vector<std::size_t> crossing_depths(const key_path &path, const std::set<std::string> &containers,
                                                const key_path &anchor, std::size_t space_size)
{
    // A concrete anchor leaf fixes the ordinal of a crossing at the anchor, so that
    // crossing gains no slot; the argv renderer drops the fixed ordinal from the flag.
    const bool               fixed = !anchor.empty() && key_path::is_indexed_segment(anchor.segments().back());
    const std::size_t        first = anchor.size() + (fixed ? 1 : 0);
    std::vector<std::size_t> depths;
    std::string              prefix;
    for(std::size_t depth = 1; depth < path.size(); ++depth)
    {
        if(!prefix.empty())
            prefix += key_path::separator;
        prefix += path.segments()[depth - 1];
        // depth counts full-path segments; the flag counts effective-path segments,
        // which drop the anchor prefix and gain the space name. A crossing above the
        // anchor is refused before the subtraction, which would wrap: it has no
        // position in the flag. A crossing at a canonical anchor leads the relative flag.
        if(containers.contains(prefix) && depth >= first)
            depths.push_back(depth - anchor.size() + space_size);
    }
    return depths;
}

inline completion_option plain_option(const key_path &path, const key_path &effective,
                                      const cli_delimiter                                   &delimiter,
                                      const std::map<std::string, std::vector<std::string>> &values,
                                      const std::map<std::string, std::string>              &descs)
{
    completion_option option{flag_of(effective, delimiter), {}, {}, {}};
    if(auto it = values.find(path.str()); it != values.end())
        option.values = it->second;
    if(auto it = descs.find(path.str()); it != descs.end())
        option.description = it->second;
    return option;
}

inline void place_slot(completion_option &option)
{
    option.ordinal_slots.push_back(option.flag.size());
    option.flag += '*';
}

inline completion_option ordinal_option(const completion_option &plain, const key_path &effective,
                                        const std::vector<std::size_t> &depths,
                                        const cli_delimiter            &delimiter)
{
    completion_option option{"--", plain.description, plain.values, {}};
    const bool        leading = std::ranges::find(depths, 0) != depths.end();
    if(leading)
        place_slot(option);
    const auto &segments = effective.segments();
    for(std::size_t i = 0; i < segments.size(); ++i)
    {
        if(i != 0 || leading)
            option.flag += delimiter.str();
        option.flag += segments[i];
        if(std::ranges::find(depths, i + 1) != depths.end())
        {
            option.flag += delimiter.str();
            place_slot(option);
        }
    }
    return option;
}

// Each path becomes its canonical flag via the same flag_of() the argv surface
// uses, so completion and the real CLI share one mapping. A non-empty space is
// prepended as the leading segment, matching the multispace_argv_source grammar.
inline completion_model completion_projection(const schema_registry &schema, std::string_view prog,
                                              const cli_delimiter &delimiter, const key_path &anchor,
                                              const key_path &space)
{
    const auto                  values     = value_sets(schema);
    const auto                  descs      = descriptions(schema);
    const std::set<std::string> containers = schema.repeated_container_paths();
    const key_path              canonical  = canonical_anchor(anchor);
    completion_model            model{std::string(prog), {}};
    for(const key_path &path : schema.surface())
    {
        // Under an anchor only strictly-descendant paths are addressable; they
        // complete by their relative flag. Value sets stay keyed by full path.
        if(!anchor_admits(path, canonical))
            continue;
        const key_path relative  = canonical.empty() ? path : path.relative_to(canonical);
        const key_path effective = space.empty() ? relative : space.join(relative);
        model.options.push_back(plain_option(path, effective, delimiter, values, descs));
        const std::vector<std::size_t> depths = crossing_depths(path, containers, anchor, space.size());
        if(!depths.empty())
            model.options.push_back(ordinal_option(model.options.back(), effective, depths, delimiter));
    }
    return model;
}

}

#endif

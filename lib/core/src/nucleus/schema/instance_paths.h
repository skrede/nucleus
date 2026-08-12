#ifndef HPP_GUARD_NUCLEUS_SCHEMA_INSTANCE_PATHS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_INSTANCE_PATHS_H

#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <set>
#include <string>

namespace nucleus {

inline std::string join_segment(const std::string &a, const std::string &b)
{
    return a.empty() ? b : a + key_path::separator + b;
}

// Declared paths of every repeated element, leaves and containers alike.
inline std::set<std::string> repeated_declared_paths(const schema_registry &schema)
{
    std::set<std::string> paths;
    for(const schema_element &el : schema.elements())
    {
        if(el.repeated)
            paths.insert(el.declared_path().str());
    }
    return paths;
}

// The innermost declared repeated path covering `canonical`: the longest member of
// `repeated_declared` that equals it or is a separator-terminated prefix of it.
// Matching leaves as well as containers is what stops a repeated leaf nested in a
// repeated container from widening its scope to the container enclosing it.
inline std::string repeated_scope_of(const std::set<std::string> &repeated_declared,
                                     const std::string &canonical)
{
    std::string scope;
    for(const std::string &declared : repeated_declared)
    {
        const std::string terminated = declared + key_path::separator;
        if((canonical == declared || canonical.starts_with(terminated))
           && declared.size() > scope.size())
            scope = declared;
    }
    return scope;
}

// The shortest prefix of `actual` whose canonical form is `declared_scope`; empty
// when no prefix reaches it. Canonical form drops both ordinals and transient key
// segments, so the returned prefix names the concrete instance the entry belongs
// to. Truncating to the scope's segment count instead lands above the container
// whenever a key segment shifts the positions.
inline std::string instance_prefix(const schema_registry &schema, const key_path &actual,
                                   const std::string &declared_scope)
{
    std::string prefix;
    for(const std::string &segment : actual.segments())
    {
        prefix = join_segment(prefix, segment);
        const auto parsed = key_path::parse(prefix);
        if(parsed && schema.canonical_text(parsed.value()) == declared_scope)
            return prefix;
    }
    return {};
}

}

#endif

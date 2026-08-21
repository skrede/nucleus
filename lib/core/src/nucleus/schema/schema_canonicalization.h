#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_CANONICALIZATION_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_CANONICALIZATION_H

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_containers.h"
#include "nucleus/schema/schema_defined_nodes.h"

#include "nucleus/keyspace/key_path.h"

#include <set>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>

namespace nucleus {

// The concrete instance path `path` names under `container`: the container
// extended by the one segment directly beneath it. Empty unless `path` is
// strictly longer than `container` and descends from it -- the length bound is
// enforced here, and it is what keeps the prefix comparison and the trailing
// index within the shorter segment vector.
inline std::string instance_under(const key_path &container, const key_path &path)
{
    if(path.size() <= container.size())
        return {};
    const std::vector<std::string> &outer = container.segments();
    const std::vector<std::string> &inner = path.segments();
    if(!std::equal(outer.begin(), outer.end(), inner.begin()))
        return {};
    std::string instance = container.str();
    if(!instance.empty())
        instance += key_path::separator;
    return instance + inner[outer.size()];
}

// `path` with the segment directly under `container` removed -- the remainder
// re-laid onto the container as if that segment were a transient key value.
inline std::string key_stripped(const key_path &container, const key_path &path)
{
    const std::vector<std::string> &inner = path.segments();
    std::string stripped = container.str();
    for(std::size_t i = container.segments().size() + 1; i < inner.size(); ++i)
    {
        if(!stripped.empty())
            stripped += key_path::separator;
        stripped += inner[i];
    }
    return stripped;
}

// Strips transient key segments from a resolved path: walking root-down, a segment
// directly under a keyed container that does not extend a declared node is an
// instance's key value (the projection consumed the key field into it) and is
// dropped; every other segment is kept. `a/b/<key>/c` with `a/b` keyed therefore
// canonicalizes to the declared `a/b/c`. The slice step uses this to re-lay a
// strain's entries onto the unified hierarchy.
inline std::string canonical_text(const key_path &path,
                                  std::span<const schema_element> declared,
                                  const schema_defined_nodes &defined)
{
    std::string canonical;
    for(const std::string &segment : path.segments())
    {
        std::string extended = canonical;
        if(!extended.empty())
            extended += key_path::separator;
        // Strip the ordinal suffix from an indexed segment before any other check.
        if(key_path::is_indexed_segment(segment))
        {
            canonical = extended + std::string(key_path::base_name(segment));
            continue;
        }
        extended += segment;
        if(has_primary_key(declared, canonical) && !defined.contains_text(extended))
            continue;
        canonical = std::move(extended);
    }
    return canonical;
}

// Whether `path` addresses content of a keyed instance of `container`: the
// container has a primary key and the path extends it by a transient key segment
// (one that is not itself a declared node). The identity presence check accepts
// such a path -- the key's value survives structurally as the instance's segment,
// not as a leaf.
inline bool keyed_instance_path(const key_path &container, const key_path &path,
                                std::span<const schema_element> declared,
                                const schema_defined_nodes &defined)
{
    if(path.size() <= container.size() || !has_primary_key(declared, container.str()))
        return false;
    const std::string instance = instance_under(container, path);
    return !instance.empty() && !defined.contains_text(instance);
}

// Detects a primary-key value that collides with a declared element name: the path
// extends a keyed container by a segment that IS a declared node (so
// keyed_instance_path cannot treat it as a transient key), yet stripping that
// segment re-lays the remainder onto a declared path -- the shape of an instance
// literally named after a sibling element (e.g. a strain keyed "port" under a
// container declaring a "port" leaf). Such an instance can never be bucketed or
// selected, so the slice step reports it loudly instead of letting validation fail
// later with an unrelated unknown-key suggestion.
inline bool key_value_collision(const key_path &container, const key_path &path,
                                std::span<const schema_element> declared,
                                const schema_defined_nodes &defined)
{
    if(path.size() <= container.size() + 1 || !has_primary_key(declared, container.str()))
        return false;
    // A non-declared segment is a true transient key value; keyed_instance_path
    // buckets it and no collision exists.
    const std::string instance = instance_under(container, path);
    if(instance.empty() || !defined.contains_text(instance))
        return false;
    // A path that is itself declared (or a prefix of a declared path) is ordinary
    // declared content, not an instance.
    if(defined.contains_text(path.str()))
        return false;
    return defined.contains_text(key_stripped(container, path));
}

inline bool ordinal_segment(const std::string &segment)
{
    return !segment.empty()
        && std::ranges::all_of(segment, [](char c) { return c >= '0' && c <= '9'; });
}

// Recognizes a CLI plain-ordinal path: a path where a digit-only segment following
// a repeated container prefix stands for an ordinal index.
// "--cluster-node-0-endpoint-port=90" maps to "cluster/node/0/endpoint/port"; that
// is recognized here as equivalent to "cluster/node/endpoint/port".
inline bool recognizes_with_ordinal(const key_path &path,
                                    std::span<const schema_element> declared,
                                    const schema_defined_nodes &defined)
{
    if(defined.declares(path.str()))
        return true; // already a declared path
    const std::set<std::string> containers = repeated_containers(declared);
    std::string collapsed;
    for(const std::string &segment : path.segments())
    {
        // An ordinal segment selects an instance of the repeated container above
        // it; the declared path does not include it.
        if(ordinal_segment(segment) && containers.contains(collapsed))
            continue;
        if(!collapsed.empty())
            collapsed += key_path::separator;
        collapsed += segment;
    }
    return collapsed != path.str() && defined.contains_text(collapsed);
}

}

#endif

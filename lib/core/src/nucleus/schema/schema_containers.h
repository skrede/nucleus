#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_CONTAINERS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_CONTAINERS_H

#include "nucleus/schema/schema.h"
#include "nucleus/schema/projection.h"

#include <set>
#include <span>
#include <string>
#include <algorithm>

namespace nucleus {

// Whether a container path has a declared primary key -- the test that makes a
// path segment under it eligible to be a transient key value.
inline bool has_primary_key(std::span<const schema_element> declared,
                            const std::string &container)
{
    return std::ranges::any_of(declared, [&](const schema_element &el) {
        return el.identity && el.container().str() == container;
    });
}

// Paths of repeated elements that are containers (at least one other element is
// anchored under them).
inline std::set<std::string> repeated_containers(std::span<const schema_element> declared)
{
    std::set<std::string> containers;
    for(const schema_element &el : declared)
    {
        if(!el.repeated)
            continue;
        const std::string dp = el.declared_path().str();
        for(const schema_element &child : declared)
        {
            if(child.container().str() == dp)
            {
                containers.insert(dp);
                break;
            }
        }
    }
    return containers;
}

// The projection a source consults to render repeatable keyed containers: for
// every primary-key element, its parent container path mapped to the key field
// name; and for every repeated container, its declared path. Built from the
// declared elements so the source need never see the registry -- the fold hands it
// across at resolve time. Empty when no primary keys are declared, leaving a
// source's structural walk unchanged.
inline schema_projection projection_of(std::span<const schema_element> declared)
{
    schema_projection proj;
    for(const schema_element &el : declared)
    {
        if(el.identity)
            proj.set_key(el.container().str(), el.name);
    }
    for(const std::string &path : repeated_containers(declared))
        proj.set_repeated_container(path);
    return proj;
}

}

#endif

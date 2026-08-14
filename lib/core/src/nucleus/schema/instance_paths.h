#ifndef HPP_GUARD_NUCLEUS_SCHEMA_INSTANCE_PATHS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_INSTANCE_PATHS_H

#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <set>
#include <span>
#include <string>
#include <vector>
#include <cstddef>

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

template<typename Canonicalize>
inline std::string qualified_scope(const key_path &actual, const key_path &declared,
                                   Canonicalize canonicalize)
{
    std::string concrete;
    std::string scope;
    for(const std::string &segment : actual.segments())
    {
        concrete = join_segment(concrete, segment);
        if(!key_path::is_indexed_segment(segment))
            continue;
        const auto canonical = key_path::parse(canonicalize(concrete));
        if(canonical && declared.starts_with(*canonical))
            scope = concrete;
    }
    return scope;
}

// Distinct concrete container-instance prefixes whose canonical form equals the
// declared container path -- correct under repeated/keyed ancestors (the prefix
// keeps the [n] ordinals; the canonical compare strips them).
inline std::vector<std::string>
instances_of(const schema_registry &schema, std::span<const std::string> keys,
             const key_path &declared)
{
    // A root-anchored container has exactly one instance: the config root. The
    // prefix scan below cannot express it (an empty prefix is not a parseable
    // key), so it is named explicitly.
    const std::size_t depth = declared.segments().size();
    if(depth == 0)
        return {std::string{}};
    std::set<std::string> prefixes;
    for(const std::string &key : keys)
    {
        auto kp = key_path::parse(key);
        if(!kp || kp->segments().size() < depth)
            continue;
        std::string prefix;
        for(std::size_t i = 0; i < depth; ++i)
            prefix = join_segment(prefix, kp->segments()[i]);
        auto pp = key_path::parse(prefix);
        if(pp && schema.canonical_text(*pp) == declared.str())
            prefixes.insert(prefix);
    }
    return {prefixes.begin(), prefixes.end()};
}

// Whether key names member_path exactly, or an indexed instance of it
// (member_path[<digits>]) -- the storage shape of a repeated member. Matches by
// the concrete instance path, so it is correct when member_path itself carries an
// ordinal/key segment (a member under a repeated/keyed container), unlike
// instances_of, whose canonical compare expects an ordinal-free declared path.
inline bool names_member_instance(const std::string &key,
                                  const std::string &member_path)
{
    if(key == member_path)
        return true;
    if(key.size() < member_path.size() + 3
       || !key.starts_with(member_path)
       || key[member_path.size()] != '[' || key.back() != ']')
        return false;
    for(std::size_t i = member_path.size() + 1; i + 1 < key.size(); ++i)
        if(key[i] < '0' || key[i] > '9')
            return false;
    return true;
}

}

#endif

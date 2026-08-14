#ifndef HPP_GUARD_NUCLEUS_SCHEMA_KEYREF_PASS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_KEYREF_PASS_H

#include "nucleus/config.h"
#include "nucleus/format.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/schema_validation.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/keyspace/key_path.h"

#include <set>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace nucleus {

class keyref_pass
{
public:
    // A keyref whose present value names no identifier in its namespace is a loud
    // dangling-reference error with a did-you-mean. An absent keyref is not dangling
    // (presence is the orthogonal `required` axis, not this check).
    static void check_keyref(const schema_registry &schema, const config &resolved,
                             const schema_element          &el,
                             std::vector<schema_violation> &out)
    {
        const identity_group_spec *group = group_of(schema, el);
        if(group == nullptr)
            return;
        const std::set<std::string>    valid = namespace_values(schema, resolved, *group);
        const std::vector<std::string> candidates(valid.begin(), valid.end());
        const std::string              declared = el.declared_path().str();
        for(const std::string &key : resolved.keys())
            check_key(schema, resolved, *group, valid, candidates, declared, key, out);
    }

private:
    static std::vector<std::string>
    instances_of(const schema_registry &schema, const config &resolved,
                 const key_path &declared)
    {
        return nucleus::instances_of(schema, resolved.keys(), declared);
    }

    // The set of identifier values present in an identity namespace within the slice
    // -- the valid targets a keyref may name. Shares the pooling shape with the
    // identity check, but keyed by value alone (the keyref cares only "does it exist").
    static std::set<std::string>
    namespace_values(const schema_registry &schema, const config &resolved,
                     const identity_group_spec &group)
    {
        const std::string     parent = group.container().str();
        std::set<std::string> values;
        for(const std::string &m : group.members)
        {
            auto member_kp = key_path::parse(join_segment(parent, m));
            if(!member_kp)
                continue;
            for(const std::string &mi : instances_of(schema, resolved, *member_kp))
                if(auto v = resolved.get(join_segment(mi, group.field)); v.has_value())
                    values.insert(*v);
        }
        return values;
    }

    static const identity_group_spec *
    group_of(const schema_registry &schema, const schema_element &el)
    {
        for(const identity_group_spec &g : schema.identity_groups())
        {
            if(g.name == el.keyref_into)
                return &g;
        }
        return nullptr;
    }

    static void check_key(const schema_registry &schema, const config &resolved,
                          const identity_group_spec      &group,
                          const std::set<std::string>    &valid,
                          const std::vector<std::string> &candidates,
                          const std::string &declared, const std::string &key,
                          std::vector<schema_violation> &out)
    {
        auto kp = key_path::parse(key);
        if(!kp || schema.canonical_text(*kp) != declared)
            return;
        auto v = resolved.get(key);
        if(!v.has_value() || valid.contains(*v))
            return;
        std::string reason = nucleus::format(
                "keyref '{}'='{}' matches no identifier in namespace '{}'",
                key, *v, group.name);
        const std::span<const std::string> cand(candidates.data(), candidates.size());
        if(auto near = suggest_keys(*v, cand, 1); !near.empty())
            reason += nucleus::format(" (did you mean '{}'?)", near.front());
        out.push_back(schema_violation{key, std::move(reason)});
    }
};

}

#endif

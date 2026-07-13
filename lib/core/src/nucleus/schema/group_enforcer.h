#ifndef HPP_GUARD_NUCLEUS_SCHEMA_GROUP_ENFORCER_H
#define HPP_GUARD_NUCLEUS_SCHEMA_GROUP_ENFORCER_H

#include "nucleus/format.h"
#include "nucleus/config.h"
#include "nucleus/config_node.h"

#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/keyspace/key_path.h"

#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// Enforces the container-scoped constraint groups and identity groups over the
// resolved, sliced configuration -- the sibling of schema_enforcer for the
// relationship-between-siblings family. Pure validation: every violation is
// collected (never thrown) and names the parties (the container instance, the
// offending members/identifiers, the group name). Borrows the schema and a
// transient config snapshot; stores neither, in keeping with the flat topology.
class group_enforcer
{
public:
    static std::vector<schema_violation>
    validate(const schema_registry &schema, const config &resolved)
    {
        std::vector<schema_violation> violations;
        for(const constraint_group &g : schema.constraint_groups())
            check_constraint_group(schema, resolved, g, violations);
        for(const identity_group_spec &g : schema.identity_groups())
            check_identity_group(schema, resolved, g, violations);
        for(const schema_element &el : schema.elements())
            if(!el.keyref_into.empty())
                check_keyref(schema, resolved, el, violations);
        return violations;
    }

private:
    static std::string join(const std::string &a, const std::string &b)
    {
        return a.empty() ? b : a + key_path::separator + b;
    }

    // Distinct concrete container-instance prefixes whose canonical form equals the
    // declared container path -- correct under repeated/keyed ancestors (the prefix
    // keeps the [n] ordinals; the canonical compare strips them).
    static std::vector<std::string>
    instances_of(const schema_registry &schema, const config &resolved,
                 const key_path &declared)
    {
        const std::size_t depth = declared.segments().size();
        // A root-anchored container has exactly one instance: the config root. The
        // prefix scan below cannot express it (an empty prefix is not a parseable
        // key), so it is named explicitly.
        if(depth == 0)
            return {std::string{}};
        std::set<std::string> prefixes;
        for(const std::string &key : resolved.keys())
        {
            auto kp = key_path::parse(key);
            if(!kp || kp->segments().size() < depth)
                continue;
            std::string prefix;
            for(std::size_t i = 0; i < depth; ++i)
                prefix = join(prefix, kp->segments()[i]);
            auto pp = key_path::parse(prefix);
            if(pp && schema.canonical_text(*pp) == declared.str())
                prefixes.insert(prefix);
        }
        return {prefixes.begin(), prefixes.end()};
    }

    static bool present(const config &resolved, const std::string &path)
    {
        return config_node(&resolved, path).exists();
    }

    // Whether key names member_path exactly, or an indexed instance of it
    // (member_path[<digits>]) -- the storage shape of a repeated member. Matches
    // by the concrete instance path, so it is correct when member_path itself
    // carries an ordinal/key segment (a member under a repeated/keyed container),
    // unlike instances_of, whose canonical compare expects an ordinal-free path.
    static bool names_member_instance(const std::string &key,
                                      const std::string &member_path)
    {
        if(key == member_path)
            return true;
        if(key.size() < member_path.size() + 3
           || key.compare(0, member_path.size(), member_path) != 0
           || key[member_path.size()] != '[' || key.back() != ']')
            return false;
        for(std::size_t i = member_path.size() + 1; i + 1 < key.size(); ++i)
            if(key[i] < '0' || key[i] > '9')
                return false;
        return true;
    }

    static std::string bundle_label(const std::vector<std::string> &names)
    {
        std::string s = "{";
        for(std::size_t i = 0; i < names.size(); ++i)
        {
            if(i)
                s += ", ";
            s += names[i];
        }
        s += "}";
        return s;
    }

    static bool satisfies(group_bound b, std::size_t bound, std::size_t active)
    {
        switch(b)
        {
            case group_bound::at_most:  return active <= bound;
            case group_bound::exactly:  return active == bound;
            case group_bound::at_least: return active >= bound;
        }
        return true;
    }

    static std::string_view bound_word(group_bound b)
    {
        switch(b)
        {
            case group_bound::at_most:  return "at most";
            case group_bound::exactly:  return "exactly";
            case group_bound::at_least: return "at least";
        }
        return "";
    }

    static void
    check_constraint_group(const schema_registry &schema, const config &resolved,
                           const constraint_group &g,
                           std::vector<schema_violation> &out)
    {
        for(const std::string &inst : instances_of(schema, resolved, g.container()))
        {
            if(g.validator)
            {
                auto verdict = g.validator(config_node(&resolved, inst));
                if(!verdict)
                    out.push_back(schema_violation{inst, nucleus::format(
                        "constraint group '{}' rejected '{}': {}",
                        g.name, inst, verdict.error())});
                continue;
            }

            std::size_t active = 0;
            std::vector<std::string> active_members;
            for(const group_member &m : g.members)
            {
                if(!m.bundle.empty())
                {
                    std::size_t present_count = 0;
                    for(const std::string &n : m.bundle)
                        if(present(resolved, join(inst, n)))
                            ++present_count;
                    if(present_count == m.bundle.size())
                    {
                        ++active;
                        active_members.push_back(bundle_label(m.bundle));
                    }
                    else if(present_count != 0)
                        out.push_back(schema_violation{inst, nucleus::format(
                            "constraint group '{}': bundle {} in '{}' is partially "
                            "present ({} of {}) -- an all_of bundle is all-or-none",
                            g.name, bundle_label(m.bundle), inst,
                            present_count, m.bundle.size())});
                    continue;
                }

                const std::string member_path = join(inst, m.name);
                bool is_active = false;
                if(m.active_value.has_value())
                {
                    const std::string &want = *m.active_value;
                    // Resolve the member's value(s) for THIS instance directly: the
                    // scalar member at member_path, or -- when it is a repeated element
                    // whose plain path carries no scalar -- its indexed instances
                    // member_path[n]. Routing member_path back through instances_of is
                    // wrong here: under a repeated/keyed container member_path already
                    // carries the ordinal/key, which the canonical compare strips. when_value
                    // matching is an exact-string compare -- unlike the case-insensitive
                    // bool converter, a value differing only in case does not activate it.
                    for(const std::string &k : resolved.keys())
                        if(names_member_instance(k, member_path))
                            if(auto v = resolved.get(k); v.has_value() && *v == want)
                            {
                                is_active = true;
                                break;
                            }
                }
                else
                    is_active = present(resolved, member_path);
                if(is_active)
                {
                    ++active;
                    active_members.push_back(m.name);
                }
            }

            if(satisfies(g.bound, g.count, active))
                continue;

            std::string which;
            for(std::size_t i = 0; i < active_members.size(); ++i)
            {
                if(i)
                    which += ", ";
                which += nucleus::format("'{}'", active_members[i]);
            }
            if(which.empty())
                which = "none";
            out.push_back(schema_violation{inst, nucleus::format(
                "constraint group '{}' on '{}' requires {} {} active member(s) but "
                "{} are active: {}",
                g.name, inst, bound_word(g.bound), g.count, active, which)});
        }
    }

    // The set of identifier values present in an identity namespace within the slice
    // -- the valid targets a keyref may name. Shares the pooling shape with the
    // identity check, but keyed by value alone (the keyref cares only "does it exist").
    static std::set<std::string>
    namespace_values(const schema_registry &schema, const config &resolved,
                     const identity_group_spec &group)
    {
        const std::string parent = group.container().str();
        std::set<std::string> values;
        for(const std::string &m : group.members)
        {
            auto member_kp = key_path::parse(join(parent, m));
            if(!member_kp)
                continue;
            for(const std::string &mi : instances_of(schema, resolved, *member_kp))
                if(auto v = resolved.get(join(mi, group.field)); v.has_value())
                    values.insert(*v);
        }
        return values;
    }

    // A keyref whose present value names no identifier in its namespace is a loud
    // dangling-reference error with a did-you-mean. An absent keyref is not dangling
    // (presence is the orthogonal `required` axis, not this check).
    static void
    check_keyref(const schema_registry &schema, const config &resolved,
                 const schema_element &el, std::vector<schema_violation> &out)
    {
        const identity_group_spec *group = nullptr;
        for(const identity_group_spec &g : schema.identity_groups())
            if(g.name == el.keyref_into) { group = &g; break; }
        if(group == nullptr)
            return;

        const std::set<std::string> valid = namespace_values(schema, resolved, *group);
        const std::vector<std::string> candidates(valid.begin(), valid.end());
        const std::string declared = el.declared_path().str();

        for(const std::string &key : resolved.keys())
        {
            auto kp = key_path::parse(key);
            if(!kp || schema.canonical_text(*kp) != declared)
                continue;
            auto v = resolved.get(key);
            if(!v.has_value() || valid.contains(*v))
                continue;
            std::string reason = nucleus::format(
                "keyref '{}'='{}' matches no identifier in namespace '{}'",
                key, *v, group->name);
            const std::span<const std::string> cand(candidates.data(), candidates.size());
            if(auto near = suggest_keys(*v, cand, 1); !near.empty())
                reason += nucleus::format(" (did you mean '{}'?)", near.front());
            out.push_back(schema_violation{key, std::move(reason)});
        }
    }

    static void
    check_identity_group(const schema_registry &schema, const config &resolved,
                         const identity_group_spec &g,
                         std::vector<schema_violation> &out)
    {
        const std::string parent = g.container().str();
        // identifier value -> [(element-type, concrete field path)]
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> pool;
        for(const std::string &m : g.members)
        {
            auto member_kp = key_path::parse(join(parent, m));
            if(!member_kp)
                continue;
            for(const std::string &mi : instances_of(schema, resolved, *member_kp))
            {
                const std::string field_path = join(mi, g.field);
                auto v = resolved.get(field_path);
                if(!v.has_value())
                {
                    out.push_back(schema_violation{mi, nucleus::format(
                        "identity group '{}': member '{}' instance '{}' is missing "
                        "its identifier field '{}'",
                        g.name, m, mi, g.field)});
                    continue;
                }
                pool[*v].emplace_back(m, field_path);
            }
        }

        for(const auto &[value, hits] : pool)
        {
            if(hits.size() <= 1)
                continue;
            std::string parties;
            for(std::size_t i = 0; i < hits.size(); ++i)
            {
                if(i)
                    parties += ", ";
                parties += nucleus::format("'{}' (element-type '{}')",
                                           hits[i].second, hits[i].first);
            }
            out.push_back(schema_violation{hits.front().second, nucleus::format(
                "identity group '{}': identifier '{}'='{}' is not unique within the "
                "slice -- declared by {}",
                g.name, g.field, value, parties)});
        }
    }
};

}

#endif

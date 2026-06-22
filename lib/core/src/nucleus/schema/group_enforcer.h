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
                    auto v = resolved.get(member_path);
                    is_active = v.has_value() && *v == *m.active_value;
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
                pool[*v].push_back({m, field_path});
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

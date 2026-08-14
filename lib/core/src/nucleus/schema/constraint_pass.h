#ifndef HPP_GUARD_NUCLEUS_SCHEMA_CONSTRAINT_PASS_H
#define HPP_GUARD_NUCLEUS_SCHEMA_CONSTRAINT_PASS_H

#include "nucleus/config.h"
#include "nucleus/format.h"
#include "nucleus/config_node.h"

#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/schema_validation.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace nucleus {

// A constraint group is evaluated independently for each concrete container
// instance so one sibling cannot satisfy another sibling's bounds.
class constraint_pass
{
public:
    static void check_constraint_group(const schema_registry         &schema,
                                       const config                  &resolved,
                                       const constraint_group        &g,
                                       std::vector<schema_violation> &out)
    {
        for(const std::string &inst : instances_of(schema, resolved, g.container()))
            check_instance(resolved, g, inst, out);
    }

private:
    static std::vector<std::string>
    instances_of(const schema_registry &schema, const config &resolved,
                 const key_path &declared)
    {
        return nucleus::instances_of(schema, resolved.keys(), declared);
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

    static void check_instance(const config &resolved, const constraint_group &g,
                               const std::string             &inst,
                               std::vector<schema_violation> &out)
    {
        if(g.validator)
        {
            auto verdict = g.validator(config_node(&resolved, inst));
            if(!verdict)
                out.push_back(schema_violation{inst, nucleus::format("constraint group '{}' rejected '{}': {}", g.name, inst, verdict.error())});
            return;
        }
        std::size_t              active = 0;
        std::vector<std::string> active_members;
        for(const group_member &m : g.members)
            check_member(resolved, g, inst, m, active, active_members, out);
        report_bound(g, inst, active, active_members, out);
    }

    static void check_member(const config &resolved, const constraint_group &g,
                             const std::string &inst, const group_member &m,
                             std::size_t                   &active,
                             std::vector<std::string>      &active_members,
                             std::vector<schema_violation> &out)
    {
        if(!m.bundle.empty())
        {
            check_bundle(resolved, g, inst, m, active, active_members, out);
            return;
        }
        if(member_is_active(resolved, inst, m))
        {
            ++active;
            active_members.push_back(m.name);
        }
    }

    static void check_bundle(const config &resolved, const constraint_group &g,
                             const std::string &inst, const group_member &m,
                             std::size_t                   &active,
                             std::vector<std::string>      &active_members,
                             std::vector<schema_violation> &out)
    {
        std::size_t present_count = 0;
        for(const std::string &n : m.bundle)
            if(present(resolved, join_segment(inst, n)))
                ++present_count;
        if(present_count == m.bundle.size())
        {
            ++active;
            active_members.push_back(bundle_label(m.bundle));
        }
        else if(present_count != 0)
            out.push_back(schema_violation{inst, nucleus::format("constraint group '{}': bundle {} in '{}' is partially "
                                                                 "present ({} of {}) -- an all_of bundle is all-or-none",
                                                                 g.name, bundle_label(m.bundle), inst, present_count, m.bundle.size())});
    }

    // Resolve the member's value(s) for this instance directly: either the scalar
    // member path or its indexed instances. The comparison is exact, unlike the
    // case-insensitive bool converter.
    static bool member_is_active(const config &resolved, const std::string &inst,
                                 const group_member &m)
    {
        const std::string member_path = join_segment(inst, m.name);
        bool              is_active   = false;
        if(m.active_value.has_value())
        {
            const std::string &want = *m.active_value;
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
        return is_active;
    }

    static void report_bound(const constraint_group &g, const std::string &inst,
                             std::size_t                     active,
                             const std::vector<std::string> &active_members,
                             std::vector<schema_violation>  &out)
    {
        if(satisfies(g.bound, g.count, active))
            return;
        std::string which;
        for(std::size_t i = 0; i < active_members.size(); ++i)
        {
            if(i)
                which += ", ";
            which += nucleus::format("'{}'", active_members[i]);
        }
        if(which.empty())
            which = "none";
        out.push_back(schema_violation{inst, nucleus::format("constraint group '{}' on '{}' requires {} {} active member(s) but "
                                                             "{} are active: {}",
                                                             g.name, inst, bound_word(g.bound), g.count, active, which)});
    }
};

}

#endif

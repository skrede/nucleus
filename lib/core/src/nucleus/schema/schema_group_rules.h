#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_GROUP_RULES_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_GROUP_RULES_H

#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/schema_attach_rules.h"
#include "nucleus/schema/schema_defined_nodes.h"

#include "nucleus/format.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/utility/escaped_text.h"

#include <string>
#include <vector>
#include <algorithm>

namespace nucleus {

inline schema_attach_result check_group_member_declared(const constraint_group &group,
                                                        const key_path &container,
                                                        const std::string &member,
                                                        const schema_defined_nodes &defined)
{
    const std::string member_path = container.empty()
        ? member : container.str() + key_path::separator + member;
    if(defined.contains_text(member_path))
        return {};
    return unexpected(nucleus::format(
        "constraint group '{}' member '{}' is not a declared "
        "element under '{}'",
        group.name, member, container.str()));
}

// A member declaring a bundle stands for every path in the bundle; a bare member
// stands for itself.
inline schema_attach_result check_group_members_declared(const constraint_group &group,
                                                         const schema_defined_nodes &defined)
{
    const key_path container = group.container();
    if(group.members.empty())
        return unexpected(nucleus::format(
            "constraint group '{}' declares no members", group.name));
    for(const group_member &m : group.members)
    {
        const std::vector<std::string> names =
            m.bundle.empty() ? std::vector<std::string>{m.name} : m.bundle;
        for(const std::string &n : names)
        {
            if(auto r = check_group_member_declared(group, container, n, defined); !r)
                return r;
        }
    }
    return {};
}

// Referential integrity for an exclusion/choice group: the anchor container must
// be an already-defined node, and every member (and bundle member) must be a
// declared element under it. A group carrying a host validator skips the member
// checks (the valve reads the resolved container directly). An empty member set
// with no validator is loud.
inline schema_attach_result check_constraint_group(const constraint_group &group,
                                                   const schema_defined_nodes &defined)
{
    const key_path container = group.container();
    if(!container.empty() && !defined.contains_node(container))
        return unexpected(nucleus::format(
            "constraint group '{}' cannot anchor under undefined keyspace '{}'",
            group.name, container.str()));
    if(group.validator)
        return {};
    return check_group_members_declared(group, defined);
}

inline schema_attach_result check_identity_members_declared(const identity_group_spec &group,
                                                            const schema_defined_nodes &defined)
{
    const key_path container = group.container();
    for(const std::string &m : group.members)
    {
        const std::string field_path = container.str() + key_path::separator
            + m + key_path::separator + group.field;
        if(!defined.contains_text(field_path))
            return unexpected(nucleus::format(
                "identity group '{}' member '{}' has no declared identifier "
                "field '{}'",
                group.name, m, group.field));
    }
    return {};
}

inline schema_attach_result check_identity_members_distinct(const identity_group_spec &group)
{
    for(auto m = group.members.begin(); m != group.members.end(); ++m)
    {
        if(std::find(group.members.begin(), m, *m) == m)
            continue;
        return unexpected(nucleus::format(
            "identity group '{}' lists member '{}' twice: a member element-type "
            "contributes its instances to the namespace once",
            escaped_text(group.name), escaped_text(*m)));
    }
    return {};
}

// A namespace name is a global handle, not a per-container one: a keyref names its
// target group by that name alone, so a second group taking a name already held
// would decide which pool a keyref resolves against by registration order.
inline schema_attach_result check_identity_group_name_unique(
    const identity_group_spec &group, const std::vector<identity_group_spec> &declared)
{
    const auto taken = std::ranges::find_if(
        declared, [&](const identity_group_spec &g) { return g.name == group.name; });
    if(taken == declared.end())
        return {};
    return unexpected(nucleus::format(
        "identity group namespace '{}' is already declared: a namespace name "
        "pools one identifier field and cannot name two groups",
        escaped_text(group.name)));
}

// Referential integrity for an identity (key) group: the parent container must be
// defined and every member element-type must declare the identifier field. A
// namespace pooling no members or no field is loud.
inline schema_attach_result check_identity_group(const identity_group_spec &group,
                                                 const schema_defined_nodes &defined)
{
    const key_path container = group.container();
    if(!container.empty() && !defined.contains_node(container))
        return unexpected(nucleus::format(
            "identity group '{}' cannot anchor under undefined keyspace '{}'",
            group.name, container.str()));
    if(group.members.empty())
        return unexpected(nucleus::format(
            "identity group '{}' declares no members", group.name));
    if(group.field.empty())
        return unexpected(nucleus::format(
            "identity group '{}' declares no identifier field", group.name));
    if(auto r = check_identity_members_distinct(group); !r)
        return r;
    return check_identity_members_declared(group, defined);
}

}

#endif

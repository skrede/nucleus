#ifndef HPP_GUARD_NUCLEUS_QUERY_SCHEMA_QUERY_CONTEXT_H
#define HPP_GUARD_NUCLEUS_QUERY_SCHEMA_QUERY_CONTEXT_H

#include "nucleus/identity.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/query/node_role.h"

#include <map>
#include <set>
#include <span>
#include <string>
#include <optional>
#include <string_view>

namespace nucleus {

// Transient join facade that carries schema authority into a query without
// storing any cross-registry pointer. Built once by config_space::query_context()
// and borrowed by the selector; must not outlive the config_space it was built from.
class schema_query_context
{
public:
    // Constructed by config_space::query_context() — the sole caller with access
    // to the claim ledger. Elements span is borrowed only for construction;
    // owner_by_canonical_path is moved in.
    schema_query_context(std::span<const schema_element> elements,
                         std::map<std::string, owner_token, std::less<>> owner_by_canonical_path,
                         std::span<const identity_group_spec> identity_groups = {})
        : m_owners(std::move(owner_by_canonical_path))
    {
        for(const identity_group_spec &g : identity_groups)
            m_namespaces.emplace(g.name, g);

        for(const schema_element &el : elements)
        {
            const std::string dp = el.declared_path().str();
            const std::string cp = el.container().str();

            // Index keyref leaves by their canonical declared path so a keyref node
            // can be dereferenced into its named identity namespace.
            if(!el.keyref_into.empty())
                m_keyref_into.emplace(dp, el.keyref_into);

            if(el.identity)
            {
                m_roles[dp]     = node_role::primary_key;
                m_pkey_field    = el.name;
                m_pkey_container = cp;
                m_keyed_containers.insert(cp);
            }
            else if(el.repeated)
            {
                // Classify as repeated_container or leaf depending on whether
                // any other element is anchored under this path.
                bool has_children = false;
                for(const schema_element &other : elements)
                {
                    if(other.container().str() == dp)
                    {
                        has_children = true;
                        break;
                    }
                }
                m_roles[dp] = has_children ? node_role::repeated_container
                                           : node_role::leaf;
                if(has_children)
                    m_repeated_containers.insert(dp);
            }
            else
            {
                bool has_children = false;
                for(const schema_element &other : elements)
                {
                    if(other.container().str() == dp)
                    {
                        has_children = true;
                        break;
                    }
                }
                m_roles[dp] = has_children ? node_role::container : node_role::leaf;
            }
        }
    }

    // Looks up the schema-authoritative role for a canonical path.
    // Returns node_role::leaf for paths not in the role index.
    node_role role_of(std::string_view canonical_path) const
    {
        auto it = m_roles.find(canonical_path);
        return it != m_roles.end() ? it->second : node_role::leaf;
    }

    // Returns the owner token for a canonical declared path, if one was recorded.
    std::optional<owner_token> owner_of(std::string_view canonical_path) const
    {
        auto it = m_owners.find(canonical_path);
        if(it != m_owners.end())
            return it->second;
        return std::nullopt;
    }

    // True when the canonical path is a declared repeated container.
    bool is_repeated_container(std::string_view canonical_path) const
    {
        return m_repeated_containers.contains(std::string(canonical_path));
    }

    // The identity group a keyref leaf (by canonical declared path) points into, or
    // nullptr when the path is not a declared keyref. Used by follow_keyref().
    const identity_group_spec *keyref_target(std::string_view canonical_path) const
    {
        auto it = m_keyref_into.find(canonical_path);
        if(it == m_keyref_into.end())
            return nullptr;
        auto ns = m_namespaces.find(it->second);
        return ns == m_namespaces.end() ? nullptr : &ns->second;
    }

    // The leaf segment of the primary key element (e.g. "name").
    std::string_view primary_key_field() const noexcept { return m_pkey_field; }

    // The container path that holds the primary key (e.g. "cluster/server").
    std::string_view primary_key_container() const noexcept { return m_pkey_container; }

    // Canonicalizes a resolved concrete path by stripping ordinal suffixes ([N])
    // and transient pkey-value segments, replicating schema_registry::canonical_text()
    // using the keyed-containers set built at construction.
    std::string canonicalize(std::string_view resolved_path) const
    {
        std::string canonical;
        std::string_view remaining = resolved_path;

        while(!remaining.empty())
        {
            auto sep = remaining.find(key_path::separator);
            std::string_view const segment = (sep == std::string_view::npos)
                ? remaining
                : remaining.substr(0, sep);

            // Strip ordinal suffix from bracket-indexed segments.
            std::string base_seg;
            if(key_path::is_indexed_segment(segment))
            {
                base_seg = std::string(key_path::base_name(segment));
                std::string candidate = canonical;
                if(!candidate.empty())
                    candidate += key_path::separator;
                candidate += base_seg;
                canonical = std::move(candidate);
            }
            else
            {
                // If parent is a keyed container and this segment is not a
                // declared node, it is a transient pkey-value segment — skip it.
                std::string extended = canonical;
                if(!extended.empty())
                    extended += key_path::separator;
                extended += std::string(segment);

                bool const parent_is_keyed = m_keyed_containers.contains(canonical);
                bool const is_declared = m_roles.contains(extended);
                if(parent_is_keyed && !is_declared)
                {
                    // Transient key-value segment: skip without advancing canonical.
                }
                else
                {
                    canonical = std::move(extended);
                }
            }

            if(sep == std::string_view::npos)
                break;
            remaining = remaining.substr(sep + 1);
        }
        return canonical;
    }

private:
    std::map<std::string, node_role, std::less<>> m_roles;
    std::map<std::string, owner_token, std::less<>> m_owners;
    std::set<std::string> m_repeated_containers;
    std::set<std::string> m_keyed_containers;
    // Identity namespaces by name, and keyref canonical declared path -> namespace name.
    std::map<std::string, identity_group_spec, std::less<>> m_namespaces;
    std::map<std::string, std::string, std::less<>> m_keyref_into;
    std::string m_pkey_field;
    std::string m_pkey_container;
};

}

#endif

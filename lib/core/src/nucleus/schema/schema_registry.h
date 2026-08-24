#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H

#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/identity.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/projection.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/schema_containers.h"
#include "nucleus/schema/schema_group_rules.h"
#include "nucleus/schema/schema_attach_rules.h"
#include "nucleus/schema/schema_defined_nodes.h"
#include "nucleus/schema/schema_canonicalization.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/registry/registration.h"

#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <variant>

namespace nucleus {

// A minimal schema registration payload retained from the facade's registration
// surface. The element-based authority below is the schema model proper; this
// keeps the path-tagged registration path the facade already exercises.
struct schema_spec
{
    std::string key_path;
};

// One of the three flat sibling registries -- and the SINGLE upstream authority.
// It stores schema elements anchored into the keyspace. Because the CLI surface
// and the document structure are both projections of these same elements, a
// registered schema dictates both simultaneously: a path is a valid document
// target iff it is a declared element, and the CLI flag set is exactly the
// declared element paths.
//
// Referential integrity is enforced at attach time: a keyspace-anchored element
// may only attach under a path that is ALREADY defined (an earlier element's
// declared path, or that path's prefix). A root-anchored element introduces its
// own top-level keyspace and so is always admissible.
//
// Holds NO reference/pointer/handle to any other registry; siblings are passed
// as parameters via the transient resolution context, never stored.
class schema_registry
{
public:
    schema_registry() = default;

    void add(schema_spec spec, owner_token owner)
    {
        // A path-tagged registration also defines that path as a recognized
        // target, so the schema-as-authority surface (recognizes / CLI gating)
        // honors paths registered through the facade, not only those attached as
        // typed elements. A malformed path is still stored as a registration but
        // contributes no recognized node.
        if(key_path::parse(spec.key_path))
            m_defined.insert(spec.key_path);
        m_entries.push_back(make_registration(std::move(spec), std::move(owner)));
    }

    std::size_t size() const noexcept { return m_entries.size(); }

    const std::vector<registration<schema_spec>> &entries() const noexcept { return m_entries; }

    schema_attach_result attach(schema_element el)
    {
        if(auto rejected = admissible(el); !rejected)
            return rejected;
        m_defined.insert(el.declared_path().str());
        m_elements.push_back(std::move(el));
        return {};
    }

    const std::vector<schema_element> &elements() const noexcept { return m_elements; }

    schema_attach_result attach_constraint_group(constraint_group group)
    {
        if(auto rejected = check_constraint_group(group, m_defined); !rejected)
            return rejected;
        m_constraint_groups.push_back(std::move(group));
        return {};
    }

    schema_attach_result attach_identity_group(identity_group_spec group)
    {
        if(auto rejected = check_identity_group(group, m_defined); !rejected)
            return rejected;
        m_identity_groups.push_back(std::move(group));
        return {};
    }

    const std::vector<constraint_group> &constraint_groups() const noexcept { return m_constraint_groups; }

    const std::vector<identity_group_spec> &identity_groups() const noexcept { return m_identity_groups; }

    schema_projection projection() const { return projection_of(m_elements); }

    // Whether a path is a declared element -- the document/CLI target test. The
    // schema is the authority: an undeclared path is not a valid target.
    bool recognizes(const key_path &path) const { return m_defined.declares(path.str()); }

    bool recognizes_with_ordinal(const key_path &path) const
    {
        return nucleus::recognizes_with_ordinal(path, m_elements, m_defined);
    }

    std::string canonical_text(const key_path &path) const
    {
        return nucleus::canonical_text(path, m_elements, m_defined);
    }

    bool keyed_instance_path(const key_path &container, const key_path &path) const
    {
        return nucleus::keyed_instance_path(container, path, m_elements, m_defined);
    }

    bool key_value_collision(const key_path &container, const key_path &path) const
    {
        return nucleus::key_value_collision(container, path, m_elements, m_defined);
    }

    // Text-keyed variant of recognizes() for callers (diagnostics) that already
    // hold a path as a string and want to tell an unknown-path violation apart
    // from a required/identity one without re-parsing.
    bool recognizes_text(const std::string &path) const { return m_defined.declares(path); }

    // The schema-projected surface: every declared element path, in canonical
    // order. The CLI surface and the document structure are both this set, which
    // is why a schema change moves both at once.
    std::vector<key_path> surface() const { return m_defined.surface(); }

    std::set<std::string> repeated_container_paths() const { return repeated_containers(m_elements); }

private:
    schema_defined_nodes m_defined;
    std::vector<schema_element> m_elements;
    std::vector<registration<schema_spec>> m_entries;
    std::vector<constraint_group> m_constraint_groups;
    std::vector<identity_group_spec> m_identity_groups;

    // Each rule reads the already-declared elements and groups rather than the
    // registry, which is what keeps the dependency one-way: the rule headers are
    // included here and never include this one.
    schema_attach_result admissible(const schema_element &el) const
    {
        if(auto r = check_anchor_wellformed(el); !r)
            return r;
        if(auto r = check_anchor_defined(el, m_defined); !r)
            return r;
        if(auto r = check_single_primary_key(el, m_elements); !r)
            return r;
        if(auto r = check_not_repeated_primary_key(el); !r)
            return r;
        if(auto r = check_not_repeated_unique(el); !r)
            return r;
        if(auto r = check_name_not_digit_led(el); !r)
            return r;
        if(auto r = check_name_wellformed(el); !r)
            return r;
        if(auto r = check_no_repeated_ancestor(el, m_elements); !r)
            return r;
        if(auto r = check_no_child_under_required(el, m_elements); !r)
            return r;
        if(auto r = check_keyref_target_registered(el, m_identity_groups); !r)
            return r;
        return check_path_not_redeclared(el, m_elements);
    }
};

}

#endif

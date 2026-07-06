#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H

#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/identity.h"

#include <cctype>

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/projection.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/keyspace/key_path.h"

#include "nucleus/registry/registration.h"

#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <variant>
#include <algorithm>

namespace nucleus {

// A minimal schema registration payload retained from the facade's registration
// surface. The element-based authority below is the schema model proper; this
// keeps the path-tagged registration path the facade already exercises.
struct schema_spec
{
    std::string key_path;
};

// The outcome of attaching a schema element: success, or a referential-integrity
// rejection naming the undefined keyspace it tried to attach under.
using schema_attach_result = expected<void, std::string>;

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

    // --- path-tagged registration surface (used by the facade) ---------------

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

    const std::vector<registration<schema_spec>> &entries() const noexcept
    {
        return m_entries;
    }

    // --- schema element authority --------------------------------------------

    // Attaches a schema element, enforcing referential integrity. A keyspace
    // anchor must resolve to an already-defined node; otherwise the attach is
    // rejected and the element is not stored.
    schema_attach_result attach(schema_element el)
    {
        if(!el.at.is_root())
        {
            const key_path &under = el.at.under();
            if(!is_defined_node(under))
            {
                return unexpected(nucleus::format(
                    "schema element '{}' cannot attach under undefined keyspace '{}'",
                    el.name, under.str()));
            }
        }

        // A config space has exactly ONE primary key: it is the single
        // slice selector for the whole schema hierarchy (many strains shipped,
        // one resolved through the key). A second identity element ANYWHERE --
        // same container or not -- would make the selector ambiguous, so it is
        // rejected at attach and the schema can never express it.
        if(el.identity)
        {
            auto existing = std::ranges::find_if(
                m_elements, [](const schema_element &e) { return e.identity; });
            if(existing != m_elements.end())
            {
                return unexpected(nucleus::format(
                    "schema element '{}' cannot be a primary key: '{}' is "
                    "already the config space's primary key, and a "
                    "space has exactly one",
                    el.name, existing->declared_path().str()));
            }
        }

        if(el.repeated && el.identity)
            return unexpected(nucleus::format(
                "schema element '{}' cannot be both repeated and a primary key: "
                "a primary key must be a unique scalar, not a collection",
                el.name));

        if(el.repeated && el.unique)
            return unexpected(nucleus::format(
                "schema element '{}' cannot be both repeated and unique: "
                "uniqueness requires a single comparable value, not a collection",
                el.name));

        // Element names must not start with a digit so CLI flag text is
        // unambiguously invertible back to a schema path (numeric leading chars
        // would collide with ordinal index notation in the CLI bijection).
        if(!el.name.empty()
           && std::isdigit(static_cast<unsigned char>(el.name.front())))
        {
            return unexpected(nucleus::format(
                "schema element '{}' has a digit-led name: element names must not "
                "start with a digit (CLI flag disambiguation requires this)",
                el.name));
        }

        // A primary key under ANY repeated ancestor is ambiguous —
        // each ordinal instance would need its own selector, which v1 does not
        // support. Walk the full ancestor chain of the container; reject at
        // attach if any ancestor is a declared repeated element.
        if(el.identity)
        {
            key_path ancestor = el.container();
            while(!ancestor.empty())
            {
                auto repeated_ancestor = std::ranges::find_if(
                    m_elements, [&](const schema_element &e) {
                        return e.repeated && e.declared_path() == ancestor;
                    });
                if(repeated_ancestor != m_elements.end())
                    return unexpected(nucleus::format(
                        "schema element '{}' is a primary key under repeated "
                        "ancestor '{}': keyed selection has no clean per-instance "
                        "meaning inside a repeated container (v1 restriction)",
                        el.name, ancestor.str()));
                ancestor = ancestor.parent();
            }
        }

        // A keyref's `into=` must name an already-registered identity group (declare
        // the namespace before the keyref that references it), mirroring the
        // declare-before-reference rule the keyspace anchor enforces.
        if(!el.keyref_into.empty())
        {
            const bool known = std::ranges::any_of(
                m_identity_groups, [&](const identity_group_spec &g) {
                    return g.name == el.keyref_into;
                });
            if(!known)
                return unexpected(nucleus::format(
                    "keyref '{}' references identity namespace '{}', which is not a "
                    "registered identity group",
                    el.name, el.keyref_into));
        }

        m_defined.insert(el.declared_path().str());
        m_elements.push_back(std::move(el));
        return {};
    }

    const std::vector<schema_element> &elements() const noexcept
    {
        return m_elements;
    }

    // --- container-scoped constraint groups -----------------------------------

    // Attaches an exclusion/choice constraint group, enforcing referential
    // integrity: the anchor container must be an already-defined node, and every
    // member (and bundle member) must be a declared element under it. A group
    // carrying a host validator skips the member checks (the valve reads the
    // resolved container directly). An empty member set with no validator is loud.
    schema_attach_result attach_constraint_group(constraint_group group)
    {
        const key_path container = group.container();
        if(!container.empty() && !is_defined_node(container))
            return unexpected(nucleus::format(
                "constraint group '{}' cannot anchor under undefined keyspace '{}'",
                group.name, container.str()));

        if(!group.validator)
        {
            if(group.members.empty())
                return unexpected(nucleus::format(
                    "constraint group '{}' declares no members", group.name));
            for(const group_member &m : group.members)
            {
                const std::vector<std::string> names =
                    m.bundle.empty() ? std::vector<std::string>{m.name} : m.bundle;
                for(const std::string &n : names)
                {
                    const std::string member_path = container.empty()
                        ? n : container.str() + key_path::separator + n;
                    if(!is_defined_text(member_path))
                        return unexpected(nucleus::format(
                            "constraint group '{}' member '{}' is not a declared "
                            "element under '{}'",
                            group.name, n, container.str()));
                }
            }
        }

        m_constraint_groups.push_back(std::move(group));
        return {};
    }

    // Attaches an identity (key) group, enforcing referential integrity: the parent
    // container must be defined and every member element-type must declare the
    // identifier field. A namespace pooling no members or no field is loud.
    schema_attach_result attach_identity_group(identity_group_spec group)
    {
        const key_path container = group.container();
        if(!container.empty() && !is_defined_node(container))
            return unexpected(nucleus::format(
                "identity group '{}' cannot anchor under undefined keyspace '{}'",
                group.name, container.str()));
        if(group.members.empty())
            return unexpected(nucleus::format(
                "identity group '{}' declares no members", group.name));
        if(group.field.empty())
            return unexpected(nucleus::format(
                "identity group '{}' declares no identifier field", group.name));
        for(const std::string &m : group.members)
        {
            const std::string field_path = container.str() + key_path::separator
                + m + key_path::separator + group.field;
            if(!is_defined_text(field_path))
                return unexpected(nucleus::format(
                    "identity group '{}' member '{}' has no declared identifier "
                    "field '{}'",
                    group.name, m, group.field));
        }
        m_identity_groups.push_back(std::move(group));
        return {};
    }

    const std::vector<constraint_group> &constraint_groups() const noexcept
    {
        return m_constraint_groups;
    }

    const std::vector<identity_group_spec> &identity_groups() const noexcept
    {
        return m_identity_groups;
    }

    // The projection a source consults to render repeatable keyed containers: for
    // every primary-key element, its parent container path mapped to the key
    // field name; and for every repeated container, its declared path. Built from
    // the schema so the source need never see the registry -- the fold hands it
    // across at resolve time. Empty when no primary keys are declared, leaving a
    // source's structural walk unchanged.
    schema_projection projection() const
    {
        schema_projection proj;
        for(const schema_element &el : m_elements)
        {
            if(el.identity)
                proj.set_key(el.container().str(), el.name);
        }
        for(const std::string &path : repeated_container_paths())
            proj.set_repeated_container(path);
        return proj;
    }

    // Whether a path is a declared element -- the document/CLI target test. The
    // schema is the authority: an undeclared path is not a valid target.
    bool recognizes(const key_path &path) const
    {
        return m_defined.contains(path.str());
    }

    // Recognizes a CLI plain-ordinal path: a path where a digit-only
    // segment following a repeated container prefix stands for an ordinal index.
    // "--cluster-node-0-endpoint-port=90" maps to "cluster/node/0/endpoint/port";
    // that is recognized here as equivalent to "cluster/node/endpoint/port".
    bool recognizes_with_ordinal(const key_path &path) const
    {
        if(recognizes(path))
            return true; // already a declared path
        // Walk segments: when a digit-only segment follows a repeated container,
        // skip it (treat as ordinal). Rebuild and test the collapsed path.
        const std::set<std::string> repeated_containers = repeated_container_paths();
        std::string collapsed;
        for(const std::string &seg : path.segments())
        {
            const bool all_digits = !seg.empty() && std::ranges::all_of(
                seg, [](char c) { return c >= '0' && c <= '9'; });
            if(all_digits && repeated_containers.contains(collapsed))
            {
                // Skip the ordinal segment -- it selects an instance of the
                // repeated container; the declared path does not include it.
                continue;
            }
            if(!collapsed.empty())
                collapsed += key_path::separator;
            collapsed += seg;
        }
        return collapsed != path.str() && is_defined_text(collapsed);
    }

    // Strips transient key segments from a resolved path: walking root-down, a
    // segment directly under a keyed container that does not extend a declared
    // node is an instance's key value (the projection consumed the key field
    // into it) and is dropped; every other segment is kept. `a/b/<key>/c` with
    // `a/b` keyed therefore canonicalizes to the declared `a/b/c`. The slice
    // step uses this to re-lay a strain's entries onto the unified hierarchy.
    std::string canonical_text(const key_path &path) const
    {
        std::string canonical;
        for(const std::string &segment : path.segments())
        {
            std::string extended = canonical;
            if(!extended.empty())
                extended += key_path::separator;
            extended += segment;

            // Strip ordinal suffix from indexed segments before any other check.
            if(key_path::is_indexed_segment(segment))
            {
                std::string base = canonical;
                if(!base.empty())
                    base += key_path::separator;
                base += std::string(key_path::base_name(segment));
                canonical = std::move(base);
                continue;
            }

            if(keyed_container(canonical) && !is_defined_text(extended))
                continue;
            canonical = std::move(extended);
        }
        return canonical;
    }

    // Whether `path` addresses content of a keyed instance of `container`: the
    // container has a primary key and the path extends it by a transient key
    // segment (one that is not itself a declared node). The identity presence
    // check accepts such a path -- the key's value survives structurally as the
    // instance's segment, not as a leaf.
    bool keyed_instance_path(const key_path &container,
                                           const key_path &path) const
    {
        if(path.size() <= container.size() || !keyed_container(container.str()))
            return false;

        const std::vector<std::string> &outer = container.segments();
        const std::vector<std::string> &inner = path.segments();
        if(!std::equal(outer.begin(), outer.end(), inner.begin()))
            return false;

        std::string instance = container.str();
        if(!instance.empty())
            instance += key_path::separator;
        instance += inner[outer.size()];
        return !is_defined_text(instance);
    }

    // Detects a primary-key value that collides with a declared element name: the
    // path extends a keyed container by a segment that IS a declared node (so
    // keyed_instance_path cannot treat it as a transient key), yet stripping that
    // segment re-lays the remainder onto a declared path -- the shape of an
    // instance literally named after a sibling element (e.g. a strain keyed
    // "port" under a container declaring a "port" leaf). Such an instance can
    // never be bucketed or selected, so the slice step reports it loudly instead
    // of letting validation fail later with an unrelated unknown-key suggestion.
    bool key_value_collision(const key_path &container,
                                           const key_path &path) const
    {
        if(path.size() <= container.size() + 1 || !keyed_container(container.str()))
            return false;

        const std::vector<std::string> &outer = container.segments();
        const std::vector<std::string> &inner = path.segments();
        if(!std::equal(outer.begin(), outer.end(), inner.begin()))
            return false;

        std::string instance = container.str();
        if(!instance.empty())
            instance += key_path::separator;
        instance += inner[outer.size()];
        // A non-declared segment is a true transient key value; keyed_instance_path
        // buckets it and no collision exists.
        if(!is_defined_text(instance))
            return false;

        // A path that is itself declared (or a prefix of a declared path) is
        // ordinary declared content, not an instance.
        if(is_defined_text(path.str()))
            return false;

        // Treat the segment as a transient key: if the remainder lands on a
        // declared path, the shape is an instance whose key value shadows the
        // declared element.
        std::string stripped = container.str();
        for(std::size_t i = outer.size() + 1; i < inner.size(); ++i)
        {
            if(!stripped.empty())
                stripped += key_path::separator;
            stripped += inner[i];
        }
        return is_defined_text(stripped);
    }

    // Text-keyed variant of recognizes() for callers (diagnostics) that already
    // hold a path as a string and want to tell an unknown-path violation apart
    // from a required/identity one without re-parsing.
    bool recognizes_text(const std::string &path) const
    {
        return m_defined.contains(path);
    }

    // The schema-projected surface: every declared element path, in canonical
    // order. The CLI surface and the document structure are both this set, which
    // is why a schema change moves both at once.
    std::vector<key_path> surface() const
    {
        std::vector<key_path> out;
        out.reserve(m_defined.size());
        for(const std::string &text : m_defined)
        {
            if(auto parsed = key_path::parse(text); parsed)
                out.push_back(std::move(parsed).value());
        }
        return out;
    }

    // Returns paths of repeated elements that are containers (at least one other
    // element is anchored under them). Used by projection() and the fold.
    std::set<std::string> repeated_container_paths() const
    {
        std::set<std::string> containers;
        for(const schema_element &el : m_elements)
        {
            if(!el.repeated)
                continue;
            const std::string dp = el.declared_path().str();
            for(const schema_element &child : m_elements)
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

private:
    // A node is "defined" if it is itself a declared element path or a prefix of
    // one (the intermediate keyspace nodes an element implies). This lets an
    // element anchor under either a leaf or an intermediate keyspace that an
    // earlier element established.
    bool is_defined_node(const key_path &node) const
    {
        return node.empty() || is_defined_text(node.str());
    }

    // m_defined is a sorted std::set, so any entry starting with a given prefix
    // sorts contiguously starting at lower_bound(prefix); checking the single
    // candidate there is equivalent to the prior any_of prefix scan.
    bool is_defined_text(const std::string &at) const
    {
        if(m_defined.contains(at))
            return true;
        const std::string below = at + key_path::separator;
        const auto it = m_defined.lower_bound(below);
        return it != m_defined.end() && it->starts_with(below);
    }

    // Whether a container path has a declared primary key -- the test that makes
    // a path segment under it eligible to be a transient key value.
    bool keyed_container(const std::string &container) const
    {
        return std::ranges::any_of(m_elements, [&](const schema_element &el) {
            return el.identity && el.container().str() == container;
        });
    }

    std::vector<registration<schema_spec>> m_entries;
    std::vector<schema_element> m_elements;
    std::vector<constraint_group> m_constraint_groups;
    std::vector<identity_group_spec> m_identity_groups;
    std::set<std::string> m_defined;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H

#include "nucleus/result.h"
#include "nucleus/format.h"
#include "nucleus/identity.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/anchor.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/registry/registration.h"

#include <set>
#include <string>
#include <vector>
#include <variant>
#include <cstddef>
#include <utility>

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
using schema_attach_result = result<std::monostate, std::string>;

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
        m_entries.push_back(make_registration(std::move(spec), std::move(owner)));
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    [[nodiscard]] const std::vector<registration<schema_spec>> &entries() const noexcept
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
                return fail(::nucleus::format(
                    "schema element '{}' cannot attach under undefined keyspace '{}'",
                    el.name, under.str()));
            }
        }
        m_defined.insert(el.declared_path().str());
        m_elements.push_back(std::move(el));
        return std::monostate{};
    }

    [[nodiscard]] const std::vector<schema_element> &elements() const noexcept
    {
        return m_elements;
    }

    // Whether a path is a declared element -- the document/CLI target test. The
    // schema is the authority: an undeclared path is not a valid target.
    [[nodiscard]] bool recognizes(const key_path &path) const
    {
        return m_defined.find(path.str()) != m_defined.end();
    }

    // The schema-projected surface: every declared element path, in canonical
    // order. The CLI surface and the document structure are both this set, which
    // is why a schema change moves both at once.
    [[nodiscard]] std::vector<key_path> surface() const
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

private:
    // A node is "defined" if it is itself a declared element path or a prefix of
    // one (the intermediate keyspace nodes an element implies). This lets an
    // element anchor under either a leaf or an intermediate keyspace that an
    // earlier element established.
    [[nodiscard]] bool is_defined_node(const key_path &node) const
    {
        if(node.empty())
            return true;
        const std::string at = node.str();
        const std::string below = at + key_path::separator;
        for(const std::string &defined : m_defined)
        {
            if(defined == at || defined.compare(0, below.size(), below) == 0)
                return true;
        }
        return false;
    }

    std::vector<registration<schema_spec>> m_entries;
    std::vector<schema_element> m_elements;
    std::set<std::string> m_defined;
};

}

#endif

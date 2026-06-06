#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_REGISTRY_H

#include "nucleus/identity.h"
#include "nucleus/registry/registration.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// A minimal schema registration payload. The schema model itself (anchored
// nodes, required, identity/selector, allowed-values) lands in a later phase;
// here it is a stub carrying just enough to prove the registration surface and
// the identity-tagged storage.
struct schema_spec
{
    std::string key_path;
};

// One of the three flat sibling registries. Constructed with only its own state
// -- it holds NO reference, pointer, or owning handle to any other registry.
// Cross-registry needs are passed as parameters via the transient resolution
// context at call time, never stored here. (This is the load-bearing invariant
// nucleus exists to enforce; the isolation test makes it executable.)
class schema_registry
{
public:
    schema_registry() = default;

    void add(schema_spec spec, owner_token owner)
    {
        m_entries.push_back(make_registration(std::move(spec), std::move(owner)));
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    [[nodiscard]] const std::vector<registration<schema_spec>> &entries() const noexcept
    {
        return m_entries;
    }

private:
    std::vector<registration<schema_spec>> m_entries;
};

}

#endif

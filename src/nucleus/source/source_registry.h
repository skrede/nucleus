#ifndef HPP_GUARD_NUCLEUS_SOURCE_SOURCE_REGISTRY_H
#define HPP_GUARD_NUCLEUS_SOURCE_SOURCE_REGISTRY_H

#include "nucleus/identity.h"
#include "nucleus/registry/registration.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// A minimal source registration payload. The runtime-virtual source/provider
// seam, capability descriptors, and the extension->parser map land in a later
// phase; here the spec is a stub naming the source. The vocabulary is kept
// deliberately format-neutral -- no parser/document coupling reaches this far.
struct source_spec
{
    std::string name;
};

// One of the three flat sibling registries. Holds NO reference/pointer/handle to
// any other registry; siblings are passed as parameters via the transient
// resolution context, never stored. See schema_registry for the invariant note.
class source_registry
{
public:
    source_registry() = default;

    void add(source_spec spec, owner_token owner)
    {
        m_entries.push_back(make_registration(std::move(spec), std::move(owner)));
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    [[nodiscard]] const std::vector<registration<source_spec>> &entries() const noexcept
    {
        return m_entries;
    }

private:
    std::vector<registration<source_spec>> m_entries;
};

}

#endif

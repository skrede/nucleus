#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_CONFIGURATION_SOURCE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_CONFIGURATION_SOURCE_H

#include "nucleus/expected.h"
#include "nucleus/capability.h"

#include "nucleus/configuration_source/inherit_declaration.h"

#include "nucleus/keyspace/entry.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// A schema-derived projection (nucleus/schema/projection.h). Forward declared so
// the seam can accept one without pulling the schema header.
class schema_projection;

// Type-erased handle owning the buffer a batch's view-values point into, keeping
// them valid until the batch is dropped (an all-owned source attaches an empty one).
// Load-bearing invariant: entries are safe to read only while the retained_buffer is
// alive; the load copies values out (value::to_owned) and then drops the batch.
class retained_buffer
{
public:
    retained_buffer() = default;

    template <typename T>
    explicit retained_buffer(std::shared_ptr<T> held) : m_held(std::move(held)) {}

    [[nodiscard]] static retained_buffer none() noexcept { return retained_buffer{}; }

    template <typename T, typename... Args>
    [[nodiscard]] static retained_buffer owning(Args &&...args)
    {
        return retained_buffer(std::make_shared<T>(std::forward<Args>(args)...));
    }

    [[nodiscard]] bool pins_anything() const noexcept { return m_held != nullptr; }

private:
    std::shared_ptr<void> m_held;
};

// The product of one pull: the entries a source produced plus the handle that
// keeps their backing buffer alive. Entries and handle travel together so the
// lifetime dependency is never separated from the data.
struct configuration_source_batch
{
    std::vector<keyspace_entry> entries;
    std::vector<extend_disposition> dispositions; // empty for flat sources
    retained_buffer buffer;
};

// The error a pull can report (e.g. a missing file or a malformed document).
using configuration_source_error = std::string;

// The result of pulling from a source.
using configuration_source_result = expected<configuration_source_batch, configuration_source_error>;

}

#endif

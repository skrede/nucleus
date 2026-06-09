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

// The runtime-virtual source/provider seam -- THE boundary of the engine. It yields
// keyspace entries (path -> value + capability flags) and a capability descriptor.
// Deliberately not a "document parser": env and argv are sources too; a document
// source is just the subcategory sharing a view-node model. A compile-time author
// writes a Parser-concept struct injected through parser_adapter<T>.
class configuration_source
{
public:
    virtual ~configuration_source() = default;

    // The affordances this source can represent. Drives feature gating.
    [[nodiscard]] virtual capability_descriptor capabilities() const = 0;

    // Offers a schema-derived projection just before pull() so a document source can
    // project repeatable keyed containers faithfully. Default no-op (flat sources
    // ignore it). Called by the load fold for every source it folds.
    virtual void apply_projection(const schema_projection &) {}

    // Returns the declared parent, if any. Called after pull(). No-op for flat sources.
    [[nodiscard]] virtual inherit_declaration inheritance() const { return {}; }

    // Produces this source's entries. On success the batch carries any retained buffer
    // the views depend on; on failure a configuration_source_error explains why.
    [[nodiscard]] virtual configuration_source_result pull() = 0;
};

}

#endif

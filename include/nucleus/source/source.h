#ifndef HPP_GUARD_NUCLEUS_SOURCE_SOURCE_H
#define HPP_GUARD_NUCLEUS_SOURCE_SOURCE_H

#include "nucleus/result.h"
#include "nucleus/capability.h"

#include "nucleus/keyspace/entry.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// A type-erased handle that pins whatever a source's view-values point into for
// the lifetime of a batch. For a zero-copy source the entries hold string_views
// into a retained buffer (raw bytes, or a parser's own document arena); this
// handle owns that buffer so the views stay valid until the batch is dropped.
// A source whose entries are all owned attaches an empty handle.
//
// The contract: a batch's entries are only safe to read while its
// retained_buffer is alive. Resolution copies values out (value::to_owned) and
// then drops the batch -- after which no view escapes. This is the single
// load-bearing lifetime invariant of the source layer.
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
struct source_batch
{
    std::vector<keyspace_entry> entries;
    retained_buffer buffer;
};

// The error a pull can report (e.g. a missing file or a malformed document).
using source_error = std::string;

// The result of pulling from a source.
using source_result = result<source_batch, source_error>;

// The runtime-virtual source/provider seam -- THE boundary of the engine. A
// source yields keyspace entries (path -> value + capability flags) from its
// input, and declares a capability descriptor. It is deliberately NOT a
// "document parser": env and argv are sources too; a document source is just
// the common subcategory that shares a view-node model. Authors who prefer a
// compile-time surface write a Parser-concept struct and inject it through
// parser_adapter<T>, which satisfies this same interface.
class source
{
public:
    virtual ~source() = default;

    // The affordances this source can represent. Drives feature gating.
    [[nodiscard]] virtual capability_descriptor capabilities() const = 0;

    // Produces this source's entries. On success the batch carries any retained
    // buffer the entries' views depend on. On failure a source_error explains
    // why (the core never silently drops a source).
    [[nodiscard]] virtual source_result pull() = 0;
};

}

#endif

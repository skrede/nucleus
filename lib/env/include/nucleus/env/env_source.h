#ifndef HPP_GUARD_NUCLEUS_ENV_ENV_SOURCE_H
#define HPP_GUARD_NUCLEUS_ENV_ENV_SOURCE_H

#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <string>
#include <utility>
#include <vector>

namespace nucleus {

// A non-document source: it emits keyspace entries directly, with no parse tree
// and no view-node model. env is the second, structurally-different source that
// proves the seam is not document-shaped -- it cannot go through the view-node
// path at all.
//
// Its capability descriptor is honestly restrictive: a flat key/value map has no
// nesting, no repeated keys in a scope (a later assignment overwrites), no typed
// scalars (every value is text), no comments, and no preserved ordering. That
// restrictiveness is the point: it is the source that actually exercises feature
// degradation, so the gating mechanism is proven against a source that genuinely
// lacks capabilities rather than one that claims to support everything.
//
// Mechanism, not policy: the source is handed an explicit list of (path, text)
// pairs. The host decides which environment variables map to which key paths and
// how names are translated -- the core never reads the process environment on
// its own or imposes a naming convention.
//
// Plain struct satisfying the source concept by duck typing.
class env_source final
{
public:
    env_source() = default;

    explicit env_source(std::vector<std::pair<std::string, std::string>> entries)
        : m_entries(std::move(entries))
    {
    }

    // Adds one host-mapped entry: a key path and its (text) value.
    env_source &set(std::string path, std::string text)
    {
        m_entries.emplace_back(std::move(path), std::move(text));
        return *this;
    }

    [[nodiscard]] static capability_descriptor descriptor() noexcept
    {
        // Deliberately empty: env supports none of the structural affordances.
        return capability_descriptor{};
    }

    [[nodiscard]] capability_descriptor capabilities() const
    {
        return descriptor();
    }

    [[nodiscard]] configuration_source_result pull()
    {
        configuration_source_batch batch;
        batch.entries.reserve(m_entries.size());
        for(const auto &[path, text] : m_entries)
            batch.entries.push_back(make_entry(path, value::owned(text), descriptor()));
        // Owned values: nothing to pin, so the batch carries no retained buffer.
        return batch;
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_PROVENANCE_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_PROVENANCE_H

#include "nucleus/identity.h"

#include <map>
#include <string>
#include <cstddef>
#include <utility>

namespace nucleus {

// The answer to "why is this value X?" for one key: the layer that won and the
// opaque owner token of the source that produced the winning value. A layer is a
// precedence rank plus a host-readable label (e.g. "argv", "base-document"); core
// stores the label verbatim and never branches on it. The token is carried but,
// per the owner_token contract, only ever compared -- never interpreted.
struct origin
{
    // Precedence rank of the winning layer (higher wins; see precedence.h).
    std::size_t rank = 0;
    // Host-readable label of the winning source/layer, surfaced verbatim.
    std::string layer;
    // The opaque owner token of the winning source.
    owner_token owner;
};

// The provenance map: key path (canonical string) -> winning origin. It is
// written in the SAME fold step that sets the value, so a value and its origin
// are recorded together and cannot diverge. Like the keyspace it is built mutable
// during resolution and copied out into the immutable configuration.
class provenance
{
public:
    provenance() = default;

    // Records (last-write-wins, mirroring the keyspace fold) the origin of the
    // value at `key`. Called in lockstep with keyspace::set so the two never
    // disagree about which source provided the winning value.
    void record(const std::string &key, origin where)
    {
        m_origins.insert_or_assign(key, std::move(where));
    }

    [[nodiscard]] const origin *of(const std::string &key) const
    {
        auto it = m_origins.find(key);
        return it == m_origins.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_origins.size(); }

    [[nodiscard]] const std::map<std::string, origin> &all() const noexcept
    {
        return m_origins;
    }

private:
    std::map<std::string, origin> m_origins;
};

}

#endif

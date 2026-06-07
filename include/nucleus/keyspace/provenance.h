#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_PROVENANCE_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_PROVENANCE_H

#include "nucleus/identity.h"

#include <map>
#include <string>
#include <vector>
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
    // disagree about which source provided the winning value. The rank of the
    // FIRST layer to set the key is retained separately across overwrites: the
    // winning origin answers "who provided this value?", the first rank answers
    // "which layer introduced this key?" -- the slice step bounds a strain's
    // defining layer by introduction, not by whoever overwrote it last.
    void record(const std::string &key, origin where)
    {
        m_first_ranks.try_emplace(key, where.rank);
        m_origins.insert_or_assign(key, std::move(where));
    }

    // Drops the origin recorded at `key` (no-op when absent) -- the counterpart
    // of record() for entries the resolve boundary re-lays under a different
    // path, so provenance never names a key the keyspace no longer holds.
    // Clears both scalar and collection origins.
    void forget(const std::string &key)
    {
        m_origins.erase(key);
        m_first_ranks.erase(key);
        m_collection_origins.erase(key);
    }

    [[nodiscard]] const origin *of(const std::string &key) const
    {
        auto it = m_origins.find(key);
        return it == m_origins.end() ? nullptr : &it->second;
    }

    // The rank of the layer that FIRST set `key`, regardless of later
    // overwrites; nullptr when the key was never recorded.
    [[nodiscard]] const std::size_t *first_rank_of(const std::string &key) const
    {
        auto it = m_first_ranks.find(key);
        return it == m_first_ranks.end() ? nullptr : &it->second;
    }

    // Records per-element origins for a repeated-path collection, replacing any
    // previously stored collection origins for this key. The first-introduction
    // rank for the path is also maintained (used by slice's Ld computation).
    void record_collection(const std::string &key, std::vector<origin> element_origins)
    {
        // Capture first rank BEFORE the move to avoid use-after-move.
        const std::size_t first_rank =
            element_origins.empty() ? 0 : element_origins.front().rank;
        m_collection_origins[key] = std::move(element_origins);
        m_first_ranks.try_emplace(key, first_rank);
    }

    // The per-element origins for a repeated-path collection, or nullptr when
    // none are recorded. Distinct from of(), which covers scalar origins only.
    [[nodiscard]] const std::vector<origin> *
    collection_origins_of(const std::string &key) const
    {
        auto it = m_collection_origins.find(key);
        return it == m_collection_origins.end() ? nullptr : &it->second;
    }

    // scalar origin count; collection_origins_of() is the separate surface for
    // repeated paths.
    [[nodiscard]] std::size_t size() const noexcept { return m_origins.size(); }

    // Returns the scalar winning-origins map. For repeated paths, scalar origins
    // are absent -- use collection_origins_of() instead.
    [[nodiscard]] const std::map<std::string, origin> &all() const noexcept
    {
        return m_origins;
    }

private:
    std::map<std::string, origin> m_origins;
    // First-introduction ranks, kept apart from the winning origins so an
    // overwrite never erases the answer to "which layer introduced this key?".
    std::map<std::string, std::size_t> m_first_ranks;
    // Per-element origins for repeated-path collections. A path appears here
    // instead of m_origins when its values are collected, not last-won.
    std::map<std::string, std::vector<origin>> m_collection_origins;
};

}

#endif

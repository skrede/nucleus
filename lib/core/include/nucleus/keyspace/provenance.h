#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_PROVENANCE_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_PROVENANCE_H

#include "nucleus/identity.h"

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>

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
    // The within-inheritance-chain layer ordinal, set only for entries that
    // came from an inheritance chain (base lowest, derived higher). Absent for
    // flat sources, which form a single flat layer exempt from re-open checks.
    // This channel is independent of `rank`: rank is cross-source precedence,
    // this is the inheritance position the slice step keys its re-open rules on.
    std::optional<std::size_t> inheritance_layer;
};

// The provenance map: key path -> winning origin, written in the SAME fold step that
// sets the value so the two cannot diverge. Like the keyspace it is built mutable
// during the load and copied out into the immutable config.
class provenance
{
public:
    provenance() = default;

    // Records (last-write-wins, in lockstep with keyspace::set) the origin at `key`.
    // The FIRST layer's rank is retained separately across overwrites: the winning
    // origin answers "who provided this value?", the first rank "which layer
    // introduced this key?" -- the slice step bounds a strain by introduction.
    void record(const std::string &key, origin where)
    {
        m_first_ranks.try_emplace(key, where.rank);
        if(where.inheritance_layer.has_value())
            m_first_inheritance_layers.try_emplace(key, where.inheritance_layer.value());
        m_origins.insert_or_assign(key, std::move(where));
    }

    // Drops the origin at `key` (no-op when absent) -- the counterpart of record()
    // for entries the load re-lays under a different path, so provenance never names
    // a key the keyspace no longer holds. Clears scalar and collection origins.
    void forget(const std::string &key)
    {
        m_origins.erase(key);
        m_first_ranks.erase(key);
        m_first_inheritance_layers.erase(key);
        m_collection_origins.erase(key);
    }

    const origin *of(const std::string &key) const
    {
        auto it = m_origins.find(key);
        return it == m_origins.end() ? nullptr : &it->second;
    }

    // The rank of the layer that FIRST set `key`, regardless of later
    // overwrites; nullptr when the key was never recorded.
    const std::size_t *first_rank_of(const std::string &key) const
    {
        auto it = m_first_ranks.find(key);
        return it == m_first_ranks.end() ? nullptr : &it->second;
    }

    // The inheritance-chain layer ordinal that FIRST set `key`, or nullptr when
    // the key was never recorded by an inheritance-chain entry. Flat sources
    // never populate this, so a flat-only key returns nullptr -- the signal the
    // slice step uses to exempt flat content from the inheritance re-open checks.
    const std::size_t *first_inheritance_layer_of(const std::string &key) const
    {
        auto it = m_first_inheritance_layers.find(key);
        return it == m_first_inheritance_layers.end() ? nullptr : &it->second;
    }

    // Records per-element origins for a repeated-path collection, replacing any
    // previously stored collection origins for this key. The first-introduction
    // rank for the path is also maintained (used by slice's Ld computation).
    void record_collection(const std::string &key, std::vector<origin> element_origins)
    {
        // Capture first rank and inheritance layer BEFORE the move.
        const std::size_t first_rank =
            element_origins.empty() ? 0 : element_origins.front().rank;
        const std::optional<std::size_t> first_layer =
            element_origins.empty() ? std::nullopt
                                    : element_origins.front().inheritance_layer;
        m_collection_origins[key] = std::move(element_origins);
        m_first_ranks.try_emplace(key, first_rank);
        if(first_layer.has_value())
            m_first_inheritance_layers.try_emplace(key, first_layer.value());
    }

    // The per-element origins for a repeated-path collection, or nullptr when
    // none are recorded. Distinct from of(), which covers scalar origins only.
    const std::vector<origin> *
    collection_origins_of(const std::string &key) const
    {
        auto it = m_collection_origins.find(key);
        return it == m_collection_origins.end() ? nullptr : &it->second;
    }

    // scalar origin count; collection_origins_of() is the separate surface for
    // repeated paths.
    std::size_t size() const noexcept { return m_origins.size(); }

    // Returns the scalar winning-origins map. For repeated paths, scalar origins
    // are absent -- use collection_origins_of() instead.
    const std::map<std::string, origin> &all() const noexcept
    {
        return m_origins;
    }

private:
    std::map<std::string, origin> m_origins;
    // First-introduction ranks, kept apart from the winning origins so an
    // overwrite never erases the answer to "which layer introduced this key?".
    std::map<std::string, std::size_t> m_first_ranks;
    // First-introduction inheritance-chain layer ordinals, present only for
    // keys an inheritance-chain entry introduced. Drives the slice step's
    // re-open detection independently of cross-source precedence rank.
    std::map<std::string, std::size_t> m_first_inheritance_layers;
    // Per-element origins for repeated-path collections. A path appears here
    // instead of m_origins when its values are collected, not last-won.
    std::map<std::string, std::vector<origin>> m_collection_origins;
};

}

#endif

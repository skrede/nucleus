#ifndef HPP_GUARD_NUCLEUS_RESOLVE_REPEATED_SWEEP_H
#define HPP_GUARD_NUCLEUS_RESOLVE_REPEATED_SWEEP_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// Owns the sweep-and-store discipline over the building keyspace: which unit a
// repeated entry belongs to, which existing entries a fresh instance displaces,
// and the paired removal and provenance forget that keep the keyspace and its
// origins from drifting apart. The keyspace, provenance and schema are borrowed,
// never owned, and must outlive this collaborator, which lives exactly as long as
// the resolve that holds it.
class repeated_sweep
{
public:
    repeated_sweep(keyspace &building, provenance &prov,
                   const schema_registry &schema) noexcept
        : m_building(building)
        , m_provenance(prov)
        , m_schema(schema)
    {
    }

    // The sweep unit an entry belongs to. A repeated container instance carries
    // fields, so it is addressable on its own; a repeated leaf ordinal carries
    // nothing, so its unit is the whole value list under one enclosing instance and
    // the trailing ordinal is dropped. Empty when the scope is unreachable.
    std::string sweep_key_of(const key_path &path, const std::string &scope,
                             bool scope_is_leaf) const
    {
        if(!scope_is_leaf)
            return instance_prefix(m_schema, path, scope);
        return join_segment(path.parent().str(),
                            std::string(key_path::base_name(path.leaf())));
    }

    // The declared form of a violation path, so a concrete instance path
    // (cluster/node[0]/port) is recognized as the element it already names and draws
    // no nearest-key suggestion. A path that does not parse falls through unchanged.
    std::string canonical_of(const std::string &path) const
    {
        const auto parsed = key_path::parse(path);
        return parsed ? m_schema.canonical_text(parsed.value()) : path;
    }

    // The declared repeated container a path names without naming one of its
    // instances, empty when every repeated-container segment carries an ordinal. A
    // repeated leaf's own segment is never a member of `containers`, so the plain
    // arrival that mints a leaf ordinal is exempt without a second rule.
    std::string unindexed_repeated_container(const std::set<std::string> &containers,
                                             const key_path &path) const
    {
        std::string prefix;
        for(const std::string &segment : path.segments())
        {
            prefix = join_segment(prefix, segment);
            const auto parsed = key_path::parse(prefix);
            if(!parsed || key_path::is_indexed_segment(segment))
                continue;
            const std::string declared = m_schema.canonical_text(parsed.value());
            if(containers.contains(declared))
                return declared;
        }
        return {};
    }

    // One keyspace scan per scope per layer: every existing entry under the scope is
    // filed under the same sweep key the entry loop derives, so each instance sweeps
    // from its bucket and resolution stays linear in the instance count.
    std::map<std::string, std::vector<key_path>>
    bucket_by_instance(const std::string &scope, bool scope_is_leaf) const
    {
        const std::string terminated = scope + key_path::separator;
        std::map<std::string, std::vector<key_path>> buckets;
        for(const key_path &existing : m_building.paths())
        {
            const std::string canonical = m_schema.canonical_text(existing);
            if(canonical != scope && !canonical.starts_with(terminated))
                continue;
            const std::string key = sweep_key_of(existing, scope, scope_is_leaf);
            if(!key.empty())
                buckets[key].push_back(existing);
        }
        return buckets;
    }

    // Removal and its provenance forget live together so the pair cannot drift.
    void sweep_instance(const std::vector<key_path> &bucket)
    {
        for(const key_path &existing : bucket)
        {
            m_building.remove(existing);
            m_provenance.forget(existing.str());
        }
    }

    // The store and its provenance record live together for the same reason.
    void store_entry(const key_path &target, std::string &&text, const layered_handle &lh)
    {
        m_building.set(target, value::owned(std::move(text)));
        m_provenance.record(target.str(),
                            origin{lh.rank, lh.label, lh.owner, lh.inheritance_layer});
    }

    // The indexed path a repeated leaf arriving without an ordinal is stored at,
    // taken from the layer's counter for its value list.
    static expected<key_path, resolve_fold_error>
    mint_leaf_ordinal(const keyspace_entry &entry, const std::string &sweep_key,
                      const std::string &label,
                      std::map<std::string, std::size_t> &counters)
    {
        if(!entry.capabilities.supports(capability::duplicate_keys)
           && counters.contains(sweep_key))
            return unexpected(error{errc::layering_violation,
                nucleus::format(
                    "source '{}': repeated field '{}' received multiple "
                    "values from a source that does not support "
                    "duplicate_keys; a flat source can supply at most one "
                    "value per repeated field per layer",
                    label, entry.path)});
        const std::string indexed =
            sweep_key + "[" + std::to_string(counters[sweep_key]++) + "]";
        auto indexed_kp = key_path::parse(indexed);
        if(!indexed_kp)
            return unexpected(error{errc::malformed_source, nucleus::format(
                "internal invariant violation: re-parsed path failed to parse "
                "in fold()'s repeated-leaf re-indexing: '{}'", indexed)});
        return std::move(indexed_kp).value();
    }

private:
    keyspace                &m_building;
    provenance              &m_provenance;
    const schema_registry   &m_schema;
};

}

#endif

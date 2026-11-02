#ifndef HPP_GUARD_NUCLEUS_RESOLVE_REPEATED_SWEEP_H
#define HPP_GUARD_NUCLEUS_RESOLVE_REPEATED_SWEEP_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/displaced_values.h"

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

class repeated_sweep
{
public:
    repeated_sweep(keyspace &building, provenance &prov, const schema_registry &schema,
                   displaced_values &displaced) noexcept
            : m_building(building)
            , m_provenance(prov)
            , m_schema(schema)
            , m_displaced(displaced)
    {
    }

    // A container instance is swept as one unit, but a repeated leaf's unit is its whole value list under
    // the enclosing instance. Empty when no prefix of the path canonicalizes to the scope.
    key_path sweep_unit_of(const key_path &path, const std::string &scope, bool scope_is_leaf) const
    {
        if(scope_is_leaf)
            return path.parent().child(std::string(key_path::base_name(path.leaf())));
        key_path prefix;
        for(const std::string &segment : path.segments())
        {
            prefix = prefix.child(segment);
            if(m_schema.canonical_text(prefix) == scope)
                return prefix;
        }
        return {};
    }

    std::string sweep_key_of(const key_path &path, const std::string &scope, bool scope_is_leaf) const
    {
        return sweep_unit_of(path, scope, scope_is_leaf).str();
    }

    // Canonicalizing a violation path lets a concrete instance path be recognized as the element it
    // names, so it draws no nearest-key suggestion.
    std::string canonical_of(const std::string &path) const
    {
        const auto parsed = key_path::parse(path);
        return parsed ? m_schema.canonical_text(parsed.value()) : path;
    }

    // A repeated leaf's own segment is never a member of `containers`, so the plain arrival that
    // mints a leaf ordinal is exempt without a second rule.
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

    // Files each entry under the sweep key the entry loop derives, so one scan per scope per layer
    // serves every instance sweep and resolution stays linear in the instance count.
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

    // A removal and its provenance forget, like a store and its provenance record, must not drift apart.
    void sweep_instance(const std::vector<key_path> &bucket, const key_path &unit,
                        const layered_handle &displacer)
    {
        m_displaced.record_sweep(unit, displacer);
        const std::string key = unit.str();
        for(const key_path &existing : bucket)
        {
            m_displaced.record(existing, m_building, m_provenance, key, displacer);
            m_building.remove(existing);
            m_provenance.forget(existing.str());
        }
    }

    void store_entry(const key_path &target, std::string &&text, const layered_handle &lh)
    {
        m_building.set(target, value::owned(std::move(text)));
        m_provenance.record(target.str(),
                            origin{lh.rank, lh.label, lh.owner, lh.inheritance_layer});
    }

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
    displaced_values        &m_displaced;
};

}

#endif

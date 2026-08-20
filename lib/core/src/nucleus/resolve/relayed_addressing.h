#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RELAYED_ADDRESSING_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RELAYED_ADDRESSING_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/repeated_sweep.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_registry.h"

#include <set>
#include <string>
#include <cstddef>

namespace nucleus {

// Answers where a keyed entry lands when the strain relay unifies it, and whether
// that landing is already taken. Displacement is bounded by the concrete instance
// of the innermost repeated scope, and candidate units are derived from raw paths
// so the keyed and unified forms of one strain remain distinct. The schema lookups
// the two questions share are refreshed once per relay rather than per entry; the
// keyspace, provenance, schema and sweep unit are borrowed, never owned.
class relayed_addressing
{
public:
    struct unified_target
    {
        key_path    path;
        std::string text;
    };

    relayed_addressing(keyspace &building, repeated_sweep &sweep, provenance &prov,
                       const schema_registry &schema) noexcept
        : m_building(building)
        , m_sweep(sweep)
        , m_provenance(prov)
        , m_schema(schema)
    {
    }

    // Refreshes the schema lookups every relayed entry shares and hands back the
    // declared repeated containers, which also bound the compaction that follows.
    const std::set<std::string> &begin_relay()
    {
        m_repeated_declared   = repeated_declared_paths(m_schema);
        m_repeated_containers = m_schema.repeated_container_paths();
        return m_repeated_containers;
    }

    // The unified path a keyed entry relays onto, kept beside the text it parsed
    // from so a caller can name it without a re-serialization round trip.
    expected<unified_target, resolve_fold_error>
    unified_path(const key_path &keyed) const
    {
        const std::string text = relay_canonical(keyed);
        auto              path = key_path::parse(text);
        if(!path)
            return unexpected(error{errc::malformed_source, nucleus::format("internal invariant violation: re-parsed path failed to parse "
                                                                            "in relay_strain()'s unification: '{}'",
                                                                            text)});
        return unified_target{std::move(path).value(), text};
    }

    // True when a higher-rank instance of the innermost declared repeated scope
    // already holds this unit. A keyed-merge collection is exempt: its instances
    // legitimately span ranks, having been finalised across layers beforehand.
    expected<bool, resolve_fold_error>
    scope_displaced(const unified_target &unified, bool keyed_merge,
                    std::size_t entry_rank) const
    {
        if(keyed_merge)
            return false;
        const std::string canonical = m_schema.canonical_text(unified.path);
        const std::string scope     = repeated_scope_of(m_repeated_declared, canonical);
        if(scope.empty())
            return false;
        const bool        scope_is_leaf = !m_repeated_containers.contains(scope);
        const std::string unit =
                m_sweep.sweep_key_of(unified.path, scope, scope_is_leaf);
        if(unit.empty())
            return unexpected(error{errc::malformed_source, nucleus::format("internal invariant violation: no prefix of '{}' canonicalizes "
                                                                            "to its declared repeated scope '{}' in relay_strain()'s displacement",
                                                                            unified.text, scope)});
        return instance_displaced(unified.path, unit, scope, scope_is_leaf, entry_rank);
    }

private:
    keyspace              &m_building;
    repeated_sweep        &m_sweep;
    provenance            &m_provenance;
    const schema_registry &m_schema;
    std::set<std::string>  m_repeated_declared;
    std::set<std::string>  m_repeated_containers;

    bool instance_displaced(const key_path &unified, const std::string &unit,
                            const std::string &scope, bool scope_is_leaf,
                            std::size_t entry_rank) const
    {
        for(const key_path &candidate : m_building.paths())
        {
            const std::string candidate_unit =
                    m_sweep.sweep_key_of(candidate, scope, scope_is_leaf);
            if(candidate_unit.empty() || candidate_unit != unit)
                continue;
            if(!scope_is_leaf && relay_canonical(candidate) != unified.str())
                continue;
            const origin *from = m_provenance.of(candidate.str());
            if(from != nullptr && from->rank > entry_rank)
                return true;
        }
        return false;
    }

    // Strips key-value segments (transient primary-key values) but preserves
    // ordinal segments, so indexed repeated leaves keep their [N] suffix.
    std::string relay_canonical(const key_path &path) const
    {
        std::string canonical;
        for(const std::string &segment : path.segments())
        {
            std::string extended = canonical;
            if(!extended.empty())
                extended += key_path::separator;
            extended += segment;
            if(!key_path::is_indexed_segment(segment)
               && keyed_instance_segment(canonical, extended))
                continue;
            canonical = std::move(extended);
        }
        return canonical;
    }

    // True when the schema says the extended path is a keyed instance path one level
    // past the keyed container, so the trailing segment is a key value, not structure.
    bool keyed_instance_segment(const std::string &canonical,
                                const std::string &extended) const
    {
        const auto canonical_kp = key_path::parse(canonical);
        if(!canonical_kp.has_value())
            return false;
        const auto ext_kp = key_path::parse(extended);
        return ext_kp.has_value()
               && m_schema.keyed_instance_path(canonical_kp.value(), ext_kp.value());
    }
};

}

#endif

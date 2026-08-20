#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RELAYED_MOVE_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RELAYED_MOVE_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// Relocates one relayed instance's whole subtree onto a new ordinal. Every value
// at or below the source is read out together with its origin, written at the
// rewritten target and erased from the source, so the keyspace and the provenance
// never drift apart mid-move. An occupied target is an invariant violation rather
// than an overwrite. The keyspace and provenance are borrowed, never owned; the
// schema is never consulted, because both paths arrive already resolved.
class relayed_move
{
    struct relayed_value
    {
        key_path path;
        value    val;
        origin   prov;
    };

public:
    relayed_move(keyspace &building, provenance &prov) noexcept
        : m_building(building)
        , m_provenance(prov)
    {
    }

    expected<void, resolve_fold_error>
    compact_relayed_instance(const key_path &source, const key_path &target)
    {
        auto values = relayed_values_of(source);
        if(!values)
            return unexpected(std::move(values).error());
        for(const relayed_value &entry : values.value())
        {
            if(auto moved = move_relayed_value(entry, source, target); !moved)
                return moved;
        }
        return {};
    }

private:
    keyspace   &m_building;
    provenance &m_provenance;

    expected<std::vector<relayed_value>, resolve_fold_error>
    relayed_values_of(const key_path &instance) const
    {
        std::vector<relayed_value> values;
        const std::string          exact = instance.str();
        const std::string          below = exact + key_path::separator;
        for(const key_path &path : m_building.paths())
        {
            if(path.str() != exact && !path.str().starts_with(below))
                continue;
            const value  *val  = m_building.find(path);
            const origin *prov = m_provenance.of(path.str());
            if(val == nullptr || prov == nullptr)
                return unexpected(error{errc::malformed_source, nucleus::format("internal invariant violation: value and provenance diverged "
                                                                                "in compact_relayed_instances(): '{}'",
                                                                                path.str())});
            values.push_back(relayed_value{path, *val, *prov});
        }
        return values;
    }

    expected<void, resolve_fold_error>
    move_relayed_value(const relayed_value &entry, const key_path &source,
                       const key_path &target)
    {
        const std::string suffix      = entry.path.str().substr(source.str().size());
        const std::string rewritten   = target.str() + suffix;
        auto              destination = key_path::parse(rewritten);
        if(!destination)
            return unexpected(error{errc::malformed_source, nucleus::format("internal invariant violation: re-parsed path failed to parse in "
                                                                            "compact_relayed_instances()'s rebuild: '{}'",
                                                                            rewritten)});
        if(m_building.contains(destination.value()))
            return unexpected(error{errc::malformed_source, nucleus::format("internal invariant violation: compact_relayed_instances() would "
                                                                            "overwrite occupied path '{}' while moving '{}'",
                                                                            rewritten, entry.path.str())});
        m_building.set(destination.value(), entry.val);
        m_provenance.record(rewritten, entry.prov);
        m_building.remove(entry.path);
        m_provenance.forget(entry.path.str());
        return {};
    }
};

}

#endif

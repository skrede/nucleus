#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_UNIQUE_FIELDS_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_UNIQUE_FIELDS_H

#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/strain_bucketing.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/schema/schema_registry.h"

#include <map>
#include <string>
#include <vector>

namespace nucleus {

// Enforces that no non-identity unique field repeats a value across a
// container's sibling strains. It BORROWS the building keyspace it reads the
// field values from and the schema that declares which fields are unique; it
// keeps no state between calls.
class strain_unique_fields
{
public:
    strain_unique_fields(const keyspace &building, const schema_registry &schema) noexcept
        : m_building(building)
        , m_schema(schema)
    {
    }

    // Runs before pruning, so every strain is still visible.
    expected<void, resolve_fold_error>
    check(const key_path &container, const strain_buckets &strains) const
    {
        for(const schema_element &el : m_schema.elements())
        {
            if(!el.unique || el.identity || el.container() != container)
                continue;
            if(auto checked = check_field(container, el, strains); !checked)
                return unexpected(checked.error());
        }
        return {};
    }

private:
    const keyspace        &m_building;
    const schema_registry &m_schema;

    expected<void, resolve_fold_error>
    check_field(const key_path &container, const schema_element &el,
                const strain_buckets &strains) const
    {
        for(const auto &[text, holders] : field_values(container, el, strains))
        {
            if(holders.size() < 2)
                continue;
            return unexpected(error{errc::layering_violation, nucleus::format(
                "unique field '{}' in container '{}' has duplicate "
                "value '{}' across instances: {}",
                el.name, container.str(), text, quoted_list(holders))});
        }
        return {};
    }

    // The unique field's path under one instance is container / key value / name.
    std::map<std::string, std::vector<std::string>>
    field_values(const key_path &container, const schema_element &el,
                 const strain_buckets &strains) const
    {
        std::map<std::string, std::vector<std::string>> holders;
        for(const auto &[key_value, _] : strains)
        {
            auto path = key_path::parse(container.str() + key_path::separator
                                        + key_value + key_path::separator + el.name);
            if(!path)
                continue;
            if(const value *v = m_building.find(path.value()))
                holders[std::string(v->text())].push_back(key_value);
        }
        return holders;
    }
};

}

#endif

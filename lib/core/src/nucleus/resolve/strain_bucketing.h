#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_BUCKETING_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_BUCKETING_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/schema/schema_registry.h"

#include <map>
#include <string>
#include <vector>
#include <string_view>

namespace nucleus {

// One container's keyed instances, bucketed by primary-key value.
using strain_buckets = std::map<std::string, std::vector<key_path>>;

inline std::string quoted_list(const std::vector<std::string> &values)
{
    std::string listed;
    for(const std::string &v : values)
    {
        if(!listed.empty())
            listed += ", ";
        listed += nucleus::format("'{}'", v);
    }
    return listed;
}

inline std::string strain_values(const strain_buckets &strains)
{
    std::vector<std::string> values;
    values.reserve(strains.size());
    for(const auto &[key_value, _] : strains)
        values.push_back(key_value);
    return quoted_list(values);
}

// Sorts one container's paths into strains. It BORROWS the building keyspace it
// snapshots and the schema it asks what a keyed instance is; it stores neither
// and keeps no state between calls.
class strain_bucketing
{
public:
    strain_bucketing(const keyspace &building, const schema_registry &schema) noexcept
        : m_building(building)
        , m_schema(schema)
    {
    }

    // paths() is a snapshot, so the caller may mutate the keyspace while holding
    // the buckets. A key value shadowing a declared element name can never be
    // bucketed and is reported here, rather than left to surface later as an
    // unrelated unknown-key suggestion.
    expected<strain_buckets, resolve_fold_error> bucket(const key_path &container) const
    {
        strain_buckets strains;
        for(const key_path &path : m_building.paths())
        {
            auto ordinal = ordinal_instance(container, path);
            if(!ordinal)
                return unexpected(ordinal.error());
            if(ordinal.value())
                continue;
            if(m_schema.keyed_instance_path(container, path))
                strains[path.segments()[container.size()]].push_back(path);
            else if(m_schema.key_value_collision(container, path))
                return unexpected(collision(container, path));
        }
        return strains;
    }

private:
    // An indexed-shaped segment is a legitimate ordinal only when a repeated
    // element is genuinely declared directly at this container under that base
    // name; otherwise it is a strain whose key value merely happens to be
    // bracket-shaped, and must not vanish here.
    expected<bool, resolve_fold_error>
    ordinal_instance(const key_path &container, const key_path &path) const
    {
        if(path.size() <= container.size()
           || !key_path::is_indexed_segment(path.segments()[container.size()]))
            return false;
        if(declares_repeated_child(container, path.segments()[container.size()]))
            return true;
        return unexpected(error{errc::schema_violation, nucleus::format(
            "primary-key value '{}' in container '{}' is shaped like a "
            "repeated-instance ordinal ('[n]'), which this container does "
            "not declare a repeated child at; rename the key value",
            path.segments()[container.size()], container.str())});
    }

    bool declares_repeated_child(const key_path &container, std::string_view seg) const
    {
        const key_path declared = container.child(std::string(key_path::base_name(seg)));
        for(const schema_element &el : m_schema.elements())
            if(el.repeated && el.declared_path() == declared)
                return true;
        return false;
    }

    static error collision(const key_path &container, const key_path &path)
    {
        return error{errc::schema_violation, nucleus::format(
            "primary-key value '{}' in container '{}' collides with "
            "a declared element of the same name: a strain cannot "
            "be keyed by a sibling element's name",
            path.segments()[container.size()], container.str())};
    }

    const keyspace        &m_building;
    const schema_registry &m_schema;
};

}

#endif

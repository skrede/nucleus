#ifndef HPP_GUARD_NUCLEUS_RUNTIME_RUNTIME_SOURCE_H
#define HPP_GUARD_NUCLEUS_RUNTIME_RUNTIME_SOURCE_H

#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// A first-class programmatic in-memory source: a host builds a configuration directly
// via .set(path, value) with no document at all (embedding code, generated config,
// tests). It emits flat (path -> value) entries, and DECLARES full structural
// capabilities (nesting, duplicate_keys, typed_scalars), because a host-built source
// genuinely CAN carry nested, repeated, and typed data. The same descriptor travels
// on every entry, so the gate's admit decision and the fold's per-entry checks
// (duplicate_keys in particular) can never disagree: two .set() calls on a repeated
// path compose instead of failing as a flat-source violation.
//
// Plain struct satisfying the source concept by duck typing.
class runtime_source final
{
public:
    runtime_source() = default;

    explicit runtime_source(std::vector<std::pair<std::string, std::string>> entries)
        : m_entries(std::move(entries))
    {
    }

    runtime_source &set(std::string path, std::string text)
    {
        m_entries.emplace_back(std::move(path), std::move(text));
        return *this;
    }

    [[nodiscard]] capability_descriptor capabilities() const
    {
        return capability_descriptor{capability::nesting,
                                     capability::duplicate_keys,
                                     capability::typed_scalars};
    }

    [[nodiscard]] configuration_source_result pull()
    {
        configuration_source_batch batch;
        batch.entries.reserve(m_entries.size());
        for(const auto &[path, text] : m_entries)
            batch.entries.push_back(make_entry(path, value::owned(text), capabilities()));
        return batch;
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

}

#endif

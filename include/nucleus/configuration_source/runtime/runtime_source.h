#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_RUNTIME_RUNTIME_SOURCE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_RUNTIME_RUNTIME_SOURCE_H

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
// tests). It emits flat (path -> value) entries exactly like env_source, but honestly
// DECLARES full structural capabilities (nesting, duplicate_keys, typed_scalars) at
// the source level, because a host-built source genuinely CAN carry nested, repeated,
// and typed data. The per-entry values carry no capability flags, so the fold result
// is identical to env's; only the gate-visible source descriptor differs, which lets
// the auto-gate admit a nested/repeated schema fed programmatically.
class runtime_source final : public configuration_source
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

    [[nodiscard]] capability_descriptor capabilities() const override
    {
        return capability_descriptor{capability::nesting,
                                     capability::duplicate_keys,
                                     capability::typed_scalars};
    }

    [[nodiscard]] configuration_source_result pull() override
    {
        configuration_source_batch batch;
        batch.entries.reserve(m_entries.size());
        for(const auto &[path, text] : m_entries)
            batch.entries.push_back(
                make_entry(path, value::owned(text), capability_descriptor{}));
        return batch;
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

}

#endif

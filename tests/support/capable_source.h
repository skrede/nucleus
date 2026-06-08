#ifndef HPP_GUARD_NUCLEUS_TESTS_SUPPORT_CAPABLE_SOURCE_H
#define HPP_GUARD_NUCLEUS_TESTS_SUPPORT_CAPABLE_SOURCE_H

#include "nucleus/capability.h"

#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus::testing {

// A flat (path -> value) feeder with the SAME emitted entries as env_source, but
// one that honestly DECLARES the structural capabilities (nesting, duplicate_keys,
// typed_scalars) at the source level. It stands in for a capable source (e.g. a
// document) in resolution tests whose subject is the fold, slice, scope, or
// concurrency behavior -- not capability gating -- so the auto-gate admits the
// nested/repeated schema they exercise. Its per-entry values carry no capability
// flags, exactly like env_source, so the fold result is byte-identical; only the
// gate-visible source descriptor differs.
class capable_source final : public nucleus::configuration_source
{
public:
    capable_source() = default;

    explicit capable_source(std::vector<std::pair<std::string, std::string>> entries)
        : m_entries(std::move(entries))
    {
    }

    capable_source &set(std::string path, std::string text)
    {
        m_entries.emplace_back(std::move(path), std::move(text));
        return *this;
    }

    [[nodiscard]] nucleus::capability_descriptor capabilities() const override
    {
        return nucleus::capability_descriptor{nucleus::capability::nesting,
                                              nucleus::capability::duplicate_keys,
                                              nucleus::capability::typed_scalars};
    }

    [[nodiscard]] nucleus::configuration_source_result pull() override
    {
        nucleus::configuration_source_batch batch;
        batch.entries.reserve(m_entries.size());
        for(const auto &[path, text] : m_entries)
            batch.entries.push_back(nucleus::make_entry(
                path, nucleus::value::owned(text), nucleus::capability_descriptor{}));
        return batch;
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

}

#endif

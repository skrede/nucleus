#ifndef HPP_GUARD_NUCLEUS_ENTRY_CONFIGURATION_H
#define HPP_GUARD_NUCLEUS_ENTRY_CONFIGURATION_H

#include "nucleus/keyspace/provenance.h"

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>

namespace nucleus {

// The immutable, self-owning resolved result of a load()/resolve().
//
// It is produced by copying OUT every resolved value into an owned std::string at
// the resolve boundary, after which the source buffers (raw bytes, document
// parser arenas) are dropped. Because it holds only owned strings -- never a
// view into any dropped buffer -- it outlives every source by construction and is
// freely, safely readable from any thread (it exposes only const reads). This is
// the same copy-out-then-freeze snapshot mechanism a future clone/transfer will
// reuse.
//
// Provenance travels with the values: alongside each value's text the
// configuration carries the origin recorded in the SAME fold step that set the
// value, so get() and provenance_of() can never disagree about a key.
class configuration
{
public:
    configuration() = default;

    configuration(std::map<std::string, std::string> values, provenance origins)
        : m_values(std::move(values)), m_provenance(std::move(origins))
    {
    }

    // The owned value at a key, or nullopt if the key carries no value. The
    // returned string is a copy -- no buffer dependency survives into it.
    [[nodiscard]] std::optional<std::string> get(const std::string &key) const
    {
        auto it = m_values.find(key);
        if(it == m_values.end())
            return std::nullopt;
        return it->second;
    }

    [[nodiscard]] bool contains(const std::string &key) const
    {
        return m_values.find(key) != m_values.end();
    }

    // "Why is this value X?" -- the winning source's origin for a key, or nullptr.
    [[nodiscard]] const origin *provenance_of(const std::string &key) const
    {
        return m_provenance.of(key);
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_values.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_values.empty(); }

    // Every key carrying a value, in canonical order.
    [[nodiscard]] std::vector<std::string> keys() const
    {
        std::vector<std::string> out;
        out.reserve(m_values.size());
        for(const auto &[key, _] : m_values)
            out.push_back(key);
        return out;
    }

private:
    std::map<std::string, std::string> m_values;
    provenance m_provenance;
};

}

#endif

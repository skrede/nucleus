#ifndef HPP_GUARD_NUCLEUS_ENTRY_CONFIGURATION_H
#define HPP_GUARD_NUCLEUS_ENTRY_CONFIGURATION_H

#include "nucleus/keyspace/provenance.h"

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <algorithm>

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

    configuration(std::map<std::string, std::string> values,
                  std::map<std::string, std::vector<std::string>> collections,
                  provenance origins)
        : m_values(std::move(values)),
          m_collections(std::move(collections)),
          m_provenance(std::move(origins))
    {
    }

    // The owned value at a key, or nullopt if the key carries no value. The
    // returned string is a copy -- no buffer dependency survives into it.
    // For repeated paths (where get_all() returns a collection), returns the
    // LAST element by precedence order.
    [[nodiscard]] std::optional<std::string> get(const std::string &key) const
    {
        auto it = m_values.find(key);
        if(it != m_values.end())
            return it->second;
        auto cit = m_collections.find(key);
        if(cit != m_collections.end() && !cit->second.empty())
            return cit->second.back();
        return std::nullopt;
    }

    // All values at a key. For repeated paths returns the full ordered collection;
    // for single-value paths returns a one-element vector; for absent paths returns
    // an empty vector.
    [[nodiscard]] std::vector<std::string> get_all(const std::string &key) const
    {
        auto cit = m_collections.find(key);
        if(cit != m_collections.end())
            return cit->second;
        auto it = m_values.find(key);
        if(it != m_values.end())
            return {it->second};
        return {};
    }

    [[nodiscard]] bool contains(const std::string &key) const
    {
        if(m_values.find(key) != m_values.end())
            return true;
        auto cit = m_collections.find(key);
        return cit != m_collections.end() && !cit->second.empty();
    }

    // "Why is this value X?" -- the winning source's origin for a scalar key, or
    // nullptr. For repeated paths (collections), returns nullptr; use
    // collection_provenance_of() instead.
    [[nodiscard]] const origin *provenance_of(const std::string &key) const
    {
        return m_provenance.of(key);
    }

    // The per-element origins for a repeated-path collection, or nullptr when
    // the key is absent or carries a scalar.
    [[nodiscard]] const std::vector<origin> *
    collection_provenance_of(const std::string &key) const
    {
        return m_provenance.collection_origins_of(key);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_values.size() + m_collections.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_values.empty() && m_collections.empty();
    }

    // Every key carrying a value or collection, in canonical order. Each repeated
    // path appears exactly once.
    [[nodiscard]] std::vector<std::string> keys() const
    {
        std::vector<std::string> out;
        out.reserve(m_values.size() + m_collections.size());
        for(const auto &[key, _] : m_values)
            out.push_back(key);
        for(const auto &[key, _] : m_collections)
            out.push_back(key);
        std::sort(out.begin(), out.end());
        return out;
    }

private:
    std::map<std::string, std::string> m_values;
    // Parallel map for resolved collections from repeated-path schema elements.
    std::map<std::string, std::vector<std::string>> m_collections;
    provenance m_provenance;
};

}

#endif

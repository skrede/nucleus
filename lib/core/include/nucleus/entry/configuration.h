#ifndef HPP_GUARD_NUCLEUS_ENTRY_CONFIGURATION_H
#define HPP_GUARD_NUCLEUS_ENTRY_CONFIGURATION_H

#include "nucleus/expected.h"

#include "nucleus/keyspace/provenance.h"

#include <any>
#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <algorithm>
#include <typeindex>

namespace nucleus {

// The immutable, self-owning result of a load. Every value is copied out into an
// owned string at the load boundary and the source buffers are dropped, so it holds
// no view into any dropped buffer, outlives every source, and is freely thread-safe
// to read (const reads only). Provenance travels with the values -- recorded in the
// same fold step -- so get() and provenance_of() can never disagree about a key.
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

    // Extended constructor carrying the typed parallel maps produced by the
    // convert() pass. The existing two-arg and three-arg constructors are
    // unchanged; this form is called by freeze() after convert() runs.
    configuration(std::map<std::string, std::string> values,
                  std::map<std::string, std::vector<std::string>> collections,
                  std::map<std::string, std::any> typed,
                  std::map<std::string, std::vector<std::any>> typed_collections,
                  provenance origins)
        : m_values(std::move(values)),
          m_collections(std::move(collections)),
          m_typed(std::move(typed)),
          m_typed_collections(std::move(typed_collections)),
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

    // Returns the typed value at `key` converted by the registered converter.
    // Errors distinguish three cases:
    //   absent path         -- the key carries no value at all
    //   no type converter   -- the key has a string value but no converter was
    //                          registered (untyped path)
    //   type mismatch       -- the stored type does not equal T (outright
    //                          type_index equality; no widening or coercion)
    // Note: any_cast<T> produces a copy of the stored value.
    template<typename T>
    [[nodiscard]] expected<T, std::string> get_as(const std::string &key) const
    {
        auto it = m_typed.find(key);
        if(it == m_typed.end())
        {
            if(m_typed_collections.find(key) != m_typed_collections.end())
                return unexpected(std::string("path '") + key
                            + "' holds a typed collection; use get_all_as<T>()");
            if(contains(key))
                return unexpected(std::string("path '") + key + "' declares no type converter");
            return unexpected(std::string("path '") + key + "' is absent");
        }
        if(it->second.type() != typeid(T))
            return unexpected(std::string("type mismatch for path '") + key
                        + "': stored type does not match requested type");
        return std::any_cast<T>(it->second);
    }

    // Returns all typed elements for a repeated path.
    // Same error distinctions as get_as<T>.
    template<typename T>
    [[nodiscard]] expected<std::vector<T>, std::string> get_all_as(const std::string &key) const
    {
        auto it = m_typed_collections.find(key);
        if(it == m_typed_collections.end())
        {
            if(m_typed.find(key) != m_typed.end())
                return unexpected(std::string("path '") + key
                            + "' holds a single typed value; use get_as<T>()");
            if(contains(key))
                return unexpected(std::string("path '") + key + "' declares no type converter");
            return unexpected(std::string("path '") + key + "' is absent");
        }
        std::vector<T> out;
        out.reserve(it->second.size());
        for(const std::any &a : it->second)
        {
            if(a.type() != typeid(T))
                return unexpected(std::string("type mismatch for path '") + key
                            + "': stored element type does not match requested type");
            out.push_back(std::any_cast<T>(a));
        }
        return out;
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
    // Parallel maps for typed values produced by the convert() pass.
    // m_typed holds scalar typed values; m_typed_collections holds per-element
    // typed values for repeated paths.
    std::map<std::string, std::any>              m_typed;
    std::map<std::string, std::vector<std::any>> m_typed_collections;
    provenance m_provenance;
};

}

#endif

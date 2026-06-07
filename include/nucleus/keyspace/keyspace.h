#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_KEYSPACE_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_KEYSPACE_H

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// The hierarchical keyspace -- the path -> value store every source writes into
// and every consumer reads from. It is build-mutable here; the resolve boundary
// copies its values out into the immutable configuration (a later phase).
//
// Hierarchy is structural, not just lexical: a value lives at a leaf path, and
// the intermediate paths that lead to it are addressable as nodes. children_of()
// surfaces the immediate sub-segments under a prefix, which is what schema
// referential integrity and the CLI surface projection walk. Ordering is
// deterministic (std::map) so iteration and diagnostics are stable.
class keyspace
{
public:
    keyspace() = default;

    // Sets the value at a leaf path (last-write-wins). Intermediate nodes are
    // implied by the path; they need no separate insertion.
    void set(const key_path &path, value v)
    {
        m_values.insert_or_assign(path.str(), std::move(v));
    }

    // Removes the value at a leaf path (no-op when none is set there). Used by
    // the resolve boundary when it re-lays entries under different paths, e.g.
    // stripping a transient key segment onto the unified hierarchy. Clears both
    // the scalar map and the collection map at this path.
    void remove(const key_path &path)
    {
        m_values.erase(path.str());
        m_collections.erase(path.str());
    }

    [[nodiscard]] bool contains(const key_path &path) const
    {
        if(m_values.find(path.str()) != m_values.end())
            return true;
        auto cit = m_collections.find(path.str());
        return cit != m_collections.end() && !cit->second.empty();
    }

    // The value at a leaf path, or nullptr if none is set there. Returns nullptr
    // for repeated paths (which hold a collection, not a scalar).
    [[nodiscard]] const value *find(const key_path &path) const
    {
        auto it = m_values.find(path.str());
        return it == m_values.end() ? nullptr : &it->second;
    }

    // Appends one value to the collection at a repeated path (within-layer
    // accumulation). Enforces the invariant that a path is scalar OR collection,
    // never both: any scalar at this path is erased first.
    void append(const key_path &path, value v)
    {
        m_values.erase(path.str());
        m_collections[path.str()].push_back(std::move(v));
    }

    // Replaces the collection at a repeated path wholesale (cross-layer replace).
    // If the replacement is empty, removes the path entirely instead. Enforces
    // the scalar/collection invariant: any scalar at this path is erased first.
    void replace_collection(const key_path &path, std::vector<value> values)
    {
        m_values.erase(path.str());
        if(values.empty())
        {
            m_collections.erase(path.str());
            return;
        }
        m_collections[path.str()] = std::move(values);
    }

    // The collection at a repeated path, or nullptr if none is set there.
    [[nodiscard]] const std::vector<value> *find_collection(const key_path &path) const
    {
        auto it = m_collections.find(path.str());
        return it == m_collections.end() ? nullptr : &it->second;
    }

    // Whether the path holds a collection (as opposed to a scalar or nothing).
    [[nodiscard]] bool is_collection(const key_path &path) const
    {
        return m_collections.find(path.str()) != m_collections.end();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_values.size() + m_collections.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_values.empty() && m_collections.empty();
    }

    // Every leaf path that carries a value or a collection, in canonical order.
    // Both maps are std::map (sorted), so a two-iterator merge walk produces a
    // sorted, deduplicated result.
    [[nodiscard]] std::vector<key_path> paths() const
    {
        std::vector<key_path> out;
        out.reserve(m_values.size() + m_collections.size());
        auto sv = m_values.begin();
        auto sc = m_collections.begin();
        while(sv != m_values.end() || sc != m_collections.end())
        {
            std::string text;
            if(sv != m_values.end() && (sc == m_collections.end() || sv->first < sc->first))
                text = (sv++)->first;
            else if(sc != m_collections.end() && (sv == m_values.end() || sc->first < sv->first))
                text = (sc++)->first;
            else // equal keys would violate the invariant; skip the duplicate gracefully
            { text = sv->first; ++sv; ++sc; }
            if(auto parsed = key_path::parse(text); parsed)
                out.push_back(std::move(parsed).value());
        }
        return out;
    }

    // Whether any leaf value or collection exists at or below the given prefix
    // node. An empty prefix asks whether the keyspace has any values at all.
    [[nodiscard]] bool has_node(const key_path &prefix) const
    {
        if(prefix.empty())
            return !m_values.empty() || !m_collections.empty();
        const std::string at = prefix.str();
        const std::string below = at + key_path::separator;
        for(const auto &[text, _] : m_values)
        {
            if(text == at || text.compare(0, below.size(), below) == 0)
                return true;
        }
        for(const auto &[text, _] : m_collections)
        {
            if(text == at || text.compare(0, below.size(), below) == 0)
                return true;
        }
        return false;
    }

    // The distinct immediate child segments directly under a prefix node. For
    // prefix a/b with leaves a/b/c and a/b/d/e this yields {c, d}. An empty
    // prefix yields the top-level segments. This is the structural step the
    // schema surface and CLI projection walk. Covers both scalar and collection
    // paths.
    [[nodiscard]] std::vector<std::string> children_of(const key_path &prefix) const
    {
        const std::size_t depth = prefix.size();
        const std::string below = prefix.empty() ? std::string()
                                                  : prefix.str() + key_path::separator;
        std::vector<std::string> out;

        auto collect_child = [&](const std::string &text) {
            if(!below.empty() && text.compare(0, below.size(), below) != 0)
                return;
            auto parsed = key_path::parse(text);
            if(!parsed)
                return;
            const auto &segs = parsed.value().segments();
            if(segs.size() <= depth)
                return;
            const std::string &child = segs[depth];
            bool seen = false;
            for(const auto &c : out)
                if(c == child) { seen = true; break; }
            if(!seen)
                out.push_back(child);
        };

        for(const auto &[text, _] : m_values)
            collect_child(text);
        for(const auto &[text, _] : m_collections)
            collect_child(text);

        return out;
    }

private:
    std::map<std::string, value> m_values;
    // Parallel map for repeated-path collections. A path is either in m_values
    // OR m_collections, never both -- this invariant is enforced at write time
    // by append(), replace_collection(), and remove().
    std::map<std::string, std::vector<value>> m_collections;
};

}

#endif

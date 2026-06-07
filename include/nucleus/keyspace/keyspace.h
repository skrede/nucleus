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
    // stripping a transient key segment onto the unified hierarchy.
    void remove(const key_path &path)
    {
        m_values.erase(path.str());
    }

    [[nodiscard]] bool contains(const key_path &path) const
    {
        return m_values.find(path.str()) != m_values.end();
    }

    // The value at a leaf path, or nullptr if none is set there.
    [[nodiscard]] const value *find(const key_path &path) const
    {
        auto it = m_values.find(path.str());
        return it == m_values.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_values.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_values.empty(); }

    // Every leaf path that carries a value, in canonical order.
    [[nodiscard]] std::vector<key_path> paths() const
    {
        std::vector<key_path> out;
        out.reserve(m_values.size());
        for(const auto &[text, _] : m_values)
        {
            if(auto parsed = key_path::parse(text); parsed)
                out.push_back(std::move(parsed).value());
        }
        return out;
    }

    // Whether any leaf value exists at or below the given prefix node. An empty
    // prefix asks whether the keyspace has any values at all.
    [[nodiscard]] bool has_node(const key_path &prefix) const
    {
        if(prefix.empty())
            return !m_values.empty();
        const std::string at = prefix.str();
        const std::string below = at + key_path::separator;
        for(const auto &[text, _] : m_values)
        {
            if(text == at || text.compare(0, below.size(), below) == 0)
                return true;
        }
        return false;
    }

    // The distinct immediate child segments directly under a prefix node. For
    // prefix a/b with leaves a/b/c and a/b/d/e this yields {c, d}. An empty
    // prefix yields the top-level segments. This is the structural step the
    // schema surface and CLI projection walk.
    [[nodiscard]] std::vector<std::string> children_of(const key_path &prefix) const
    {
        const std::size_t depth = prefix.size();
        const std::string below = prefix.empty() ? std::string()
                                                  : prefix.str() + key_path::separator;
        std::vector<std::string> out;
        for(const auto &[text, _] : m_values)
        {
            if(!below.empty() && text.compare(0, below.size(), below) != 0)
                continue;
            auto parsed = key_path::parse(text);
            if(!parsed)
                continue;
            const auto &segs = parsed.value().segments();
            if(segs.size() <= depth)
                continue;
            const std::string &child = segs[depth];
            if(out.empty() || out.back() != child)
            {
                bool seen = false;
                for(const auto &c : out)
                    if(c == child) { seen = true; break; }
                if(!seen)
                    out.push_back(child);
            }
        }
        return out;
    }

private:
    std::map<std::string, value> m_values;
};

}

#endif

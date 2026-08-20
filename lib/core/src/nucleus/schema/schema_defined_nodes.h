#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_DEFINED_NODES_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_DEFINED_NODES_H

#include "nucleus/keyspace/key_path.h"

#include <set>
#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// The keyspace paths a schema has declared, and the membership questions asked of
// them. Owns its set outright and borrows nothing, so a registry can hold one by
// value and stay default-constructible.
class schema_defined_nodes
{
public:
    void insert(std::string path) { m_defined.insert(std::move(path)); }

    bool declares(const std::string &at) const { return m_defined.contains(at); }

    // A node is "defined" if it is itself a declared element path or a prefix of
    // one (the intermediate keyspace nodes an element implies). This lets an
    // element anchor under either a leaf or an intermediate keyspace that an
    // earlier element established.
    bool contains_node(const key_path &node) const
    {
        return node.empty() || contains_text(node.str());
    }

    // m_defined is a sorted std::set, so any entry starting with a given prefix
    // sorts contiguously starting at lower_bound(prefix); checking the single
    // candidate there is equivalent to the prior any_of prefix scan.
    bool contains_text(const std::string &at) const
    {
        if(m_defined.contains(at))
            return true;
        const std::string below = at + key_path::separator;
        const auto it = m_defined.lower_bound(below);
        return it != m_defined.end() && it->starts_with(below);
    }

    std::vector<key_path> surface() const
    {
        std::vector<key_path> out;
        out.reserve(m_defined.size());
        for(const std::string &text : m_defined)
        {
            if(auto parsed = key_path::parse(text); parsed)
                out.push_back(std::move(parsed).value());
        }
        return out;
    }

private:
    std::set<std::string> m_defined;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_CONFIG_NODE_H
#define HPP_GUARD_NUCLEUS_CONFIG_NODE_H

#include "nucleus/error.h"
#include "nucleus/expected.h"
#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstddef>
#include <optional>
#include <functional>
#include <algorithm>
#include <string_view>
#include <type_traits>
#include <set>

namespace nucleus {

// Forward-declared: config_node holds a pointer but all method bodies that call
// config live in config.h after the full config definition (avoids circular include).
class config;

// The structural role of a node in the resolved configuration tree.
enum class node_kind
{
    scalar,    // Leaf value: has a string value, no children.
    container, // Plain struct: has named children, no ordinal index.
    repeated,  // Ordered collection: has N indexed instances (node[0], node[1], ...).
};

// Enter/leave walker for depth-first tree traversal.
// Pattern: enter/leave callbacks with recurse-on-enter semantics.
class config_tree_walker
{
public:
    virtual ~config_tree_walker() = default;

    // Called on descent into a node. Return true to recurse into children.
    virtual bool enter(const class config_node &node) = 0;

    // Called on ascent from a node, after all children have been visited.
    virtual void leave(const class config_node &node) = 0;
};

// A cheap, value-semantic cursor into the resolved configuration tree.
// Holds a const pointer to the immutable config and an owned path string.
// Lifetime: must not outlive the config object it was derived from.
//
// Navigation never fails loudly -- absent keys yield an invalid view that
// propagates through further navigation. Terminal as<T>() returns
// expected<T, error> with errc::absent_key carrying the full attempted path.
//
// Method bodies that call config APIs are defined in config.h after the full
// config class definition, to break the mutual include dependency.
class config_node
{
public:
    // Default: invalid / null-view node.
    config_node() noexcept = default;

    // Internal constructor: used by config::root() and navigation operators.
    config_node(const config *cfg, std::string path)
        : m_config(cfg), m_path(std::move(path))
    {}

    // Navigate to a named child. Never fails; use exists() to test reachability.
    config_node operator[](std::string_view name) const
    {
        if(!m_config)
            return config_node{};
        std::string child = m_path.empty()
            ? std::string(name)
            : (m_path + key_path::separator + std::string(name));
        return config_node{m_config, std::move(child)};
    }

    // Navigate to the Nth instance of a repeated node. Returns a null-view
    // (m_config == nullptr) when this node is not repeated or the index is absent.
    config_node operator[](std::size_t index) const;

    // True when the node corresponds to a path that exists in the config.
    bool exists() const noexcept;

    // Structural classification of this node.
    node_kind kind() const noexcept;

    // Number of instances for a repeated node; 1 for scalars/containers; 0 for absent.
    std::size_t count() const noexcept;

    // Direct children of this node, in ordinal (repeated) or canonical (container) order.
    // For repeated nodes: one config_node per distinct ordinal, sorted numerically.
    // For container nodes: one config_node per distinct immediate child name.
    // For scalars / absent nodes: empty.
    std::vector<config_node> children() const;

    // The key path that identifies this node in the resolved keyspace.
    std::string_view path() const noexcept { return m_path; }

    // The raw string value for a scalar node, or nullopt for containers/absent.
    std::optional<std::string> value() const;

    // The typed value at this scalar path. Delegates to config::get_as<T>().
    // Returns errc::absent_key carrying the full attempted path when this node
    // is invalid (null-view) or the path is absent.
    template<typename T>
    expected<T, error> as() const;

    // Pre-order depth-first visit. Calls fn(*this); if fn returns false, stops.
    // Otherwise recurses into children() in ordinal/canonical order.
    void visit(std::function<bool(const config_node &)> fn) const
    {
        if(!fn(*this))
            return;
        for(const config_node &child : children())
            child.visit(fn);
    }

    // Enter/leave depth-first walk. Calls walker.enter(*this); if it returns
    // true, recurses into children; then calls walker.leave(*this).
    void walk(config_tree_walker &walker) const
    {
        if(walker.enter(*this))
        {
            for(const config_node &child : children())
                child.walk(walker);
        }
        walker.leave(*this);
    }

    // The parent node: strips the trailing path segment. Root (empty path) and
    // null nodes return a null node. Correct for indexed segments: parent of
    // "cluster/node[0]" is "cluster", handled entirely by key_path::parent().
    config_node parent() const;

    // Nearest ancestor (walking toward root) whose leaf segment base name (ordinal
    // stripped) matches `name`. Returns a null node when no ancestor matches.
    // One shared walk routine reused by the relative-reference resolver and the query API.
    config_node ancestor(std::string_view name) const;

private:
    // Returns distinct ordinal values for this repeated node, sorted numerically.
    std::vector<std::size_t> distinct_ordinals() const;

    const config *m_config = nullptr;
    std::string   m_path;
};

} // namespace nucleus

// Method bodies that use the full config API are defined in config.h.
// Include guard prevents re-inclusion; the bodies are injected after config is complete.

#endif

#ifndef HPP_GUARD_NUCLEUS_SCHEMA_PROJECTION_H
#define HPP_GUARD_NUCLEUS_SCHEMA_PROJECTION_H

#include <map>
#include <set>
#include <string>
#include <cstddef>
#include <utility>
#include <string_view>

namespace nucleus {

// A schema-derived view the load fold hands a source (via apply_projection) just
// before pulling, so a document source can distinguish container instances instead
// of collapsing repeated siblings last-wins. It carries one fact per container path:
// the name of its PRIMARY KEY field, so a source emits cluster/server/<name>/... per
// instance rather than overwriting one cluster/server/.... Domain-neutral: only
// keyspace paths and declared field names; an empty projection leaves the default
// structural walk unchanged.
class schema_projection
{
public:
    // Declares that the container at `container_path` is keyed by the field named
    // `key_field`. The first declaration for a container wins; a container has at
    // most one primary key (the schema enforces this at attach).
    void set_key(std::string container_path, std::string key_field)
    {
        m_keys.emplace(std::move(container_path), std::move(key_field));
    }

    // The primary-key field name for a container path, or nullptr when the
    // container is not keyed (the source should fall back to its structural walk).
    const std::string *key_of(std::string_view container_path) const
    {
        auto it = m_keys.find(std::string(container_path));
        return it == m_keys.end() ? nullptr : &it->second;
    }

    // Marks a container path as repeated; a source's walk assigns ordinals to
    // sibling elements at this path.
    void set_repeated_container(std::string container_path)
    {
        m_repeated_containers.insert(std::move(container_path));
    }

    // True when container_path has been declared as a repeated container.
    bool is_repeated_container(std::string_view container_path) const
    {
        return m_repeated_containers.find(std::string(container_path))
               != m_repeated_containers.end();
    }

    bool empty() const noexcept { return m_keys.empty() && m_repeated_containers.empty(); }
    std::size_t size() const noexcept { return m_keys.size(); }

private:
    std::map<std::string, std::string> m_keys;
    std::set<std::string> m_repeated_containers;
};

}

#endif

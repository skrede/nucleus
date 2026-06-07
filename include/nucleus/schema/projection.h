#ifndef HPP_GUARD_NUCLEUS_SCHEMA_PROJECTION_H
#define HPP_GUARD_NUCLEUS_SCHEMA_PROJECTION_H

#include <map>
#include <string>
#include <cstddef>
#include <utility>
#include <string_view>

namespace nucleus {

// A schema-derived view a source consults to project repeatable containers
// faithfully. The schema is the single authority over document structure, but a
// source is built before resolve and cannot see the schema directly; the resolve
// fold hands it this projection (via source::apply_projection) just before
// pulling, so a document source can distinguish container instances instead of
// collapsing repeated siblings last-wins.
//
// It carries one fact: for a container path (the keyspace path of a repeatable
// element), the name of its PRIMARY KEY field -- the child whose value names which
// instance an entry belongs to. A repeatable `server` element under `cluster`
// with primary key `name` registers container "cluster/server" -> key "name", so
// a source emits `cluster/server/<name-value>/...` for each instance rather than
// overwriting one `cluster/server/...`.
//
// Domain-neutral: it names no format and no host vocabulary, only keyspace paths
// and field names the schema declared. An empty projection (no keyed containers,
// or no schema) leaves a source's default structural walk unchanged.
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
    [[nodiscard]] const std::string *key_of(std::string_view container_path) const
    {
        auto it = m_keys.find(std::string(container_path));
        return it == m_keys.end() ? nullptr : &it->second;
    }

    [[nodiscard]] bool empty() const noexcept { return m_keys.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_keys.size(); }

private:
    std::map<std::string, std::string> m_keys;
};

}

#endif

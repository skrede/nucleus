#ifndef HPP_GUARD_NUCLEUS_CONFIG_H
#define HPP_GUARD_NUCLEUS_CONFIG_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/config_node.h"

#include "nucleus/config_source/degradation.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/provenance.h"
#include "nucleus/keyspace/ordinal_sort_key.h"

#include <any>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <algorithm>
#include <functional>
#include <string_view>

namespace nucleus {

// The immutable, self-owning result of a load. Every value is copied out into an
// owned string at the load boundary and the source buffers are dropped, so it holds
// no view into any dropped buffer, outlives every source, and is freely thread-safe
// to read (const reads only). Provenance travels with the values -- recorded in the
// same fold step -- so get() and provenance_of() can never disagree about a key.
//
// Repeated paths (both repeated containers and repeated leaves) are stored as
// indexed scalars in m_values: "config/tags[0]"="a", "config/tags[1]"="b",
// "cluster/node[0]/port"="80", "cluster/node[1]/port"="90". get_all() gathers
// them by scanning for keys whose canonical form matches and sorting by numeric
// ordinal -- not by lexicographic map order, which breaks for ordinals >= 10.
class config
{
public:
    config() = default;

    // Entry point for the walk API. Returns a root-anchored cursor backed by
    // this immutable config. The cursor holds a const pointer to *this; the config
    // must outlive all cursors derived from it.
    config_node root() const noexcept
    {
        return config_node{this, std::string{}};
    }

    config(std::map<std::string, std::string> values, provenance origins)
        : m_values(std::make_move_iterator(values.begin()),
                   std::make_move_iterator(values.end())),
          m_provenance(std::move(origins))
    {
    }

    // Extended constructor carrying the typed parallel map produced by convert()
    // and the soft-capability degradations recorded during load.
    config(std::map<std::string, std::string> values,
           std::map<std::string, std::any> typed,
           provenance origins,
           std::vector<degradation> degraded = {})
        : m_values(std::make_move_iterator(values.begin()),
                   std::make_move_iterator(values.end())),
          m_typed(std::make_move_iterator(typed.begin()),
                  std::make_move_iterator(typed.end())),
          m_provenance(std::move(origins)),
          m_degradations(std::move(degraded))
    {
    }

    // The owned value at a key, or nullopt if the key carries no value. The
    // returned string is a copy -- no buffer dependency survives into it.
    // Returns nullopt for absent keys and for unindexed paths crossing a repeated
    // container (legacy untyped surface). Use get_as() for the typed loud error
    // naming the container and instance count, or get("path[N]") / get_all() for
    // indexed access.
    std::optional<std::string> get(std::string_view key) const
    {
        auto it = m_values.find(key);
        if(it != m_values.end())
            return it->second;
        return std::nullopt;
    }

    // All values matching the path's explicit ordinals and omitted ordinal slots.
    // Results follow the complete numeric ordinal tuple across nested repeats.
    std::vector<std::string> get_all(std::string_view key) const
    {
        auto direct = m_values.find(key);
        if(direct != m_values.end())
            return {direct->second};

        std::vector<std::pair<ordinal_key, std::string>> gathered;
        for(const auto &[k, v] : m_values)
        {
            if(gather_path_matches(k, key))
                gathered.emplace_back(ordinal_sort_key(k), v);
        }
        return ordered_gather(std::move(gathered));
    }

    bool contains(std::string_view key) const
    {
        return m_values.contains(key);
    }

    // "Why is this value X?" -- the winning source's origin for a scalar key, or
    // nullptr. For indexed paths (e.g. "cluster/node[0]/port"), returns the origin
    // for that specific indexed path.
    const origin *provenance_of(std::string_view key) const
    {
        return m_provenance.of(std::string(key));
    }

    // Returns the typed value at `key` converted by the registered converter.
    // Errors distinguish four cases:
    //   errc::absent_key -- the key carries no value at all
    //   errc::index_required -- the path crosses a repeated container without an
    //                              index; message names the container and instance count
    //   errc::missing_converter -- the key has a string value but no converter
    //                              was registered (untyped path)
    //   errc::mismatched_type -- the stored type does not equal T (outright
    //                              exact stored type; no widening or coercion)
    // Note: any_cast<T> produces a copy of the stored value.
    template<typename T>
    expected<T, error> get_as(std::string_view key) const
    {
        auto it = m_typed.find(key);
        if(it == m_typed.end())
        {
            if(contains(key))
                return unexpected(error{errc::missing_converter,
                            "path '" + std::string(key) + "' declares no type converter"});
            if(auto crossing = crossing_repeated_container(key); crossing)
                return unexpected(error{errc::index_required,
                    nucleus::format(
                        "path '{}' crosses repeated container '{}' "
                        "-- index required, {} instance(s)",
                        key, crossing->path, crossing->count)});
            return unexpected(error{errc::absent_key,
                        "path '" + std::string(key) + "' is absent"});
        }
        const T *typed = std::any_cast<T>(&it->second);
        if(typed == nullptr)
            return unexpected(error{errc::mismatched_type,
                        "type mismatch for path '" + std::string(key)
                        + "': stored type does not match requested type"});
        return *typed;
    }

    // Returns typed elements matching every explicit ordinal in full tuple order.
    template<typename T>
    expected<std::vector<T>, error> get_all_as(const std::string &key) const
    {
        std::vector<std::pair<ordinal_key, T>> gathered;
        for(const auto &[k, v] : m_typed)
        {
            if(!gather_path_matches(k, key))
                continue;
            const T *typed = std::any_cast<T>(&v);
            if(typed == nullptr)
                return unexpected(error{errc::mismatched_type,
                            std::string("type mismatch for path '") + k
                            + "': stored element type does not match requested type"});
            gathered.emplace_back(ordinal_sort_key(k), *typed);
        }

        if(!gathered.empty())
            return ordered_gather(std::move(gathered));

        for(const auto &[candidate, ignored] : m_values)
            if(gather_path_matches(candidate, key))
                return unexpected(error{errc::missing_converter,
                            std::string("path '") + key + "' declares no type converter"});
        return unexpected(error{errc::absent_key,
                    std::string("path '") + key + "' is absent"});
    }

    std::size_t size() const noexcept { return m_values.size(); }

    bool empty() const noexcept { return m_values.empty(); }

    // Every key carrying a value, in canonical order (sorted by string key).
    std::vector<std::string> keys() const
    {
        std::vector<std::string> out;
        out.reserve(m_values.size());
        for(const auto &[key, _] : m_values)
            out.push_back(key);
        return out;
    }

    // The soft-capability degradations recorded during load -- load-level
    // provenance for why a value's shape changed. Empty when nothing degraded.
    std::span<const degradation> degradations() const noexcept
    {
        return m_degradations;
    }

private:
    friend class config_node;

    struct repeated_container_crossing
    {
        std::string path;
        std::size_t count;
    };

    using ordinal_key = std::vector<std::pair<std::string, std::size_t>>;

    static bool gather_path_matches(std::string_view candidate,
                                    std::string_view query)
    {
        auto candidate_path = key_path::parse(candidate);
        auto query_path = key_path::parse(query);
        if(!candidate_path || !query_path
           || candidate_path.value().size() != query_path.value().size())
            return false;
        for(std::size_t i = 0; i < query_path.value().size(); ++i)
        {
            const auto &left = candidate_path.value().segments()[i];
            const auto &right = query_path.value().segments()[i];
            if(key_path::base_name(left) != key_path::base_name(right))
                return false;
            if(key_path::is_indexed_segment(right)
               && (!key_path::is_indexed_segment(left)
                   || key_path::ordinal_of(left) != key_path::ordinal_of(right)))
                return false;
        }
        return true;
    }

    template<typename T>
    static std::vector<T> ordered_gather(
        std::vector<std::pair<ordinal_key, T>> gathered)
    {
        std::stable_sort(gathered.begin(), gathered.end(),
            [](const auto &left, const auto &right) {
                return left.first < right.first;
            });
        std::vector<T> result;
        result.reserve(gathered.size());
        for(auto &[order, value] : gathered)
            result.push_back(std::move(value));
        return result;
    }

    // The eager, already-sorted value map, shared with config_node navigation so
    // it reaches keys through ordered lower_bound range scans instead of copying
    // every key out via keys(). Not exposed publicly.
    const std::map<std::string, std::string, std::less<>> &
    ordered_values() const noexcept
    {
        return m_values;
    }

    static std::optional<std::size_t> first_omitted_ordinal(
        const key_path &candidate, const key_path &query)
    {
        for(std::size_t i = 0; i < query.size(); ++i)
            if(key_path::is_indexed_segment(candidate.segments()[i])
               && !key_path::is_indexed_segment(query.segments()[i]))
                return i;
        return std::nullopt;
    }

    static std::optional<key_path> matching_prefix(
        std::string_view candidate, const key_path &query)
    {
        auto parsed = key_path::parse(candidate);
        if(!parsed || parsed->size() < query.size())
            return std::nullopt;
        std::vector<std::string> segments(
                parsed->segments().begin(), parsed->segments().begin()
                    + static_cast<std::ptrdiff_t>(query.size()));
        key_path prefix(std::move(segments));
        if(!gather_path_matches(prefix.str(), query.str()))
            return std::nullopt;
        return prefix;
    }

    static void record_omitted_ordinal(
        const key_path &candidate, const key_path &query,
        std::optional<std::size_t> &first, std::set<std::size_t> &ordinals)
    {
        auto omitted = first_omitted_ordinal(candidate, query);
        if(!omitted || (first && omitted.value() > first.value()))
            return;
        if(!first || omitted.value() < first.value())
        {
            first = omitted;
            ordinals.clear();
        }
        ordinals.insert(key_path::ordinal_of(candidate.segments()[first.value()]));
    }

    std::optional<repeated_container_crossing> crossing_repeated_container(
        std::string_view key) const
    {
        auto query = key_path::parse(key);
        if(!query)
            return std::nullopt;
        std::optional<std::size_t> first;
        std::set<std::size_t> ordinals;
        for(const auto &[candidate_text, ignored] : m_values)
        {
            auto candidate = matching_prefix(candidate_text, query.value());
            if(candidate)
                record_omitted_ordinal(
                        candidate.value(), query.value(), first, ordinals);
        }
        if(!first)
            return std::nullopt;
        std::vector<std::string> segments(
                query->segments().begin(), query->segments().begin()
                    + static_cast<std::ptrdiff_t>(first.value() + 1));
        return repeated_container_crossing{
                key_path(std::move(segments)).str(), ordinals.size()};
    }

    std::map<std::string, std::string, std::less<>> m_values;
    // Typed values produced by the convert() pass. Indexed paths are keyed by
    // their full indexed path string (e.g. "cluster/node[0]/port").
    std::map<std::string, std::any, std::less<>> m_typed;
    provenance m_provenance;
    std::vector<degradation> m_degradations;
};

}

// ---------------------------------------------------------------------------
// config_node method bodies -- defined here so they have access to the full
// config class definition. config_node.h only forward-declares config.
// ---------------------------------------------------------------------------

namespace nucleus {

inline config_node config_node::operator[](std::size_t index) const
{
    if(!m_config || kind() != node_kind::repeated)
        return config_node{};
    const auto ordinals = distinct_ordinals();
    if(std::find(ordinals.begin(), ordinals.end(), index) == ordinals.end())
        return config_node{};
    return config_node{m_config, m_path + "[" + std::to_string(index) + "]"};
}

inline bool config_node::exists() const noexcept
{
    if(!m_config)
        return false;
    const auto &values = m_config->ordered_values();
    // Root node (empty path) exists when the config has at least one key.
    if(m_path.empty())
        return !values.empty();
    if(values.contains(m_path))
        return true;
    // A container/repeated node owns no key of its own; it exists iff some key
    // begins with its bracket or child prefix. Prefixed keys are contiguous and
    // are the smallest keys >= the prefix, so one lower_bound probe suffices.
    const std::string indexed_prefix = m_path + "[";
    auto it = values.lower_bound(indexed_prefix);
    if(it != values.end() && it->first.starts_with(indexed_prefix))
        return true;
    const std::string child_prefix = m_path + key_path::separator;
    it = values.lower_bound(child_prefix);
    return it != values.end() && it->first.starts_with(child_prefix);
}

inline node_kind config_node::kind() const noexcept
{
    if(!m_config || !exists())
        return node_kind::scalar;
    // Root (empty path) is always a container -- it has no index.
    if(m_path.empty())
        return node_kind::container;
    const auto &values = m_config->ordered_values();
    const std::string indexed_prefix = m_path + "[";
    auto it = values.lower_bound(indexed_prefix);
    if(it != values.end() && it->first.starts_with(indexed_prefix))
        return node_kind::repeated;
    const std::string child_prefix = m_path + key_path::separator;
    it = values.lower_bound(child_prefix);
    if(it != values.end() && it->first.starts_with(child_prefix))
        return node_kind::container;
    return node_kind::scalar;
}

inline std::size_t config_node::count() const noexcept
{
    if(!m_config || !exists())
        return 0;
    if(kind() != node_kind::repeated)
        return 1;
    return distinct_ordinals().size();
}

inline std::vector<config_node> config_node::children() const
{
    if(!m_config || !exists())
        return {};

    const node_kind k = kind();

    if(k == node_kind::repeated)
    {
        std::vector<std::size_t> const ordinals = distinct_ordinals();
        std::vector<config_node> result;
        result.reserve(ordinals.size());
        for(std::size_t const ord : ordinals)
            result.emplace_back(m_config,
                m_path + "[" + std::to_string(ord) + "]");
        return result;
    }

    if(k == node_kind::container)
    {
        // For the root node (empty path), every key is a direct child.
        // Otherwise, walk only the contiguous m_path + '/' prefix range.
        const auto &values = m_config->ordered_values();
        const bool is_root = m_path.empty();
        const std::string child_prefix = is_root ? std::string{} : (m_path + key_path::separator);
        std::set<std::string> seen_names;
        for(auto it = values.lower_bound(child_prefix);
            it != values.end() && it->first.starts_with(child_prefix); ++it)
        {
            const std::string &key = it->first;
            std::string_view const remainder(key.data() + child_prefix.size(),
                                       key.size() - child_prefix.size());
            // Take only the first segment (stop at separator or '[').
            const auto sep_pos = remainder.find(key_path::separator);
            const auto idx_pos = remainder.find('[');
            const std::size_t end = std::min(
                sep_pos == std::string_view::npos ? remainder.size() : sep_pos,
                idx_pos == std::string_view::npos ? remainder.size() : idx_pos);
            seen_names.emplace(remainder.substr(0, end));
        }
        std::vector<config_node> result;
        result.reserve(seen_names.size());
        for(const std::string &name : seen_names)
        {
            const std::string child_path = is_root ? name : (m_path + key_path::separator + name);
            result.emplace_back(m_config, child_path);
        }
        return result;
    }

    return {};
}

inline std::optional<std::string> config_node::value() const
{
    if(!m_config)
        return std::nullopt;
    return m_config->get(m_path);
}

template<typename T>
inline expected<T, error> config_node::as() const
{
    if(!m_config)
        return unexpected(error{errc::absent_key,
            "path '" + m_path + "' is absent"});
    // For std::string, return the raw scalar value directly so callers do not
    // need to register a string converter just to read a plain text field.
    if constexpr(std::is_same_v<T, std::string>)
    {
        auto raw = m_config->get(m_path);
        if(raw.has_value())
            return std::move(*raw);
        // Fall through to get_as<T> for the proper error (index_required,
        // absent_key, etc.) from the canonical error surface.
    }
    return m_config->get_as<T>(m_path);
}

inline std::vector<std::size_t> config_node::distinct_ordinals() const
{
    const auto &values = m_config->ordered_values();
    const std::string indexed_prefix = m_path + "[";
    std::set<std::size_t> ordinal_set;
    for(auto it = values.lower_bound(indexed_prefix);
        it != values.end() && it->first.starts_with(indexed_prefix); ++it)
    {
        const std::string &k = it->first;
        // Remainder after m_path: "[N]" or "[N]/...".
        std::string_view const remainder(k.data() + m_path.size(),
                                   k.size() - m_path.size());
        if(remainder.size() < 3 || remainder[0] != '[')
            continue;
        const auto close = remainder.find(']');
        if(close == std::string_view::npos || close < 2)
            continue;
        const std::string_view digits = remainder.substr(1, close - 1);
        bool all_digits = !digits.empty();
        for(char const c : digits)
            if(c < '0' || c > '9') { all_digits = false; break; }
        if(!all_digits)
            continue;
        std::size_t ordinal = 0;
        for(char const c : digits)
            ordinal = (ordinal * 10) + static_cast<std::size_t>(c - '0');
        ordinal_set.insert(ordinal);
    }
    return {ordinal_set.begin(), ordinal_set.end()};
}

inline config_node config_node::parent() const
{
    if(!m_config || m_path.empty())
        return config_node{};
    auto kp = key_path::parse(m_path);
    if(!kp)
        return config_node{};
    key_path const p = kp.value().parent();
    if(p.empty())
        return config_node{m_config, std::string{}};
    return config_node{m_config, p.str()};
}

inline config_node config_node::ancestor(std::string_view name) const
{
    // rel:./x sugar: '.' segment handled in resolve_relative_path() only; key_path::parse accepts it as a plain segment
    config_node cur = parent();
    while(cur.m_config)
    {
        if(cur.m_path.empty())
            break;
        auto kp = key_path::parse(cur.m_path);
        if(!kp)
            break;
        if(key_path::base_name(kp.value().leaf()) == name)
            return cur;
        cur = cur.parent();
    }
    return config_node{};
}

} // namespace nucleus

#endif

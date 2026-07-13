#ifndef HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H
#define HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/ordinal_sort_key.h"

#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <ostream>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

// A flat source has no nesting: the anchor path becomes the key. An element is a
// leaf iff no other declared element is anchored beneath it; container elements
// carry no value of their own and so produce no line.
inline bool is_flat_leaf(const schema_element &el,
                                       std::span<const schema_element> all)
{
    const std::string prefix = el.declared_path().str() + key_path::separator;
    for(const schema_element &other : all)
    {
        const std::string path = other.declared_path().str();
        if(path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0)
            return false;
    }
    return true;
}

// Strips `anchor` (a canonical '/'-joined prefix) off `key` in place. Returns
// false for a key not strictly under the anchor -- such a key is not addressable
// in an anchored flat grammar, so the caller skips it. An empty anchor keeps
// every key absolute.
inline bool strip_flat_anchor(std::string_view &key,
                                            std::string_view anchor)
{
    if(anchor.empty())
        return true;
    if(key.size() <= anchor.size() + 1 || !key.starts_with(anchor)
       || key[anchor.size()] != key_path::separator)
        return false;
    key.remove_prefix(anchor.size() + 1);
    return true;
}

// Renders a '/'-joined key with `key_separator` standing in for every separator,
// so a flat format can speak its own delimiter (argv flags) over the same paths.
// Indexed segments (e.g. "node[0]") are stripped to their base name ("node") so
// repeated instances render at the same canonical key (e.g. "cluster/node/port").
inline std::string render_flat_key(std::string_view key,
                                                 std::string_view key_separator)
{
    std::string out;
    out.reserve(key.size());
    std::size_t start = 0;
    bool first_seg = true;
    for(std::size_t i = 0; i <= key.size(); ++i)
    {
        if(i == key.size() || key[i] == key_path::separator)
        {
            std::string_view seg = key.substr(start, i - start);
            std::string_view base = key_path::base_name(seg);
            if(!first_seg)
                out.append(key_separator);
            out.append(base);
            first_seg = false;
            start = i + 1;
        }
    }
    return out;
}

// Projects the declared schema into flat KEY= template lines: one line per declared
// LEAF path (its path joined by `key_separator`), blank value (template only), with
// `key_prefix` prepended to every key. A constrained leaf annotates its allowed
// set as a trailing `# allowed: a|b|c`.
inline expected<void, error> emit_flat_template(const config_space &space, std::ostream &out,
                               std::string_view key_prefix,
                               std::string_view key_separator = "/",
                               std::string_view anchor = {})
{
    const std::span<const schema_element> elements = space.schema_elements();
    for(const schema_element &el : elements)
    {
        if(!is_flat_leaf(el, elements))
            continue;
        const std::string full = el.declared_path().str();
        std::string_view key = full;
        if(!strip_flat_anchor(key, anchor))
            continue;
        out << key_prefix << render_flat_key(key, key_separator) << '=';
        if(!el.allowed_values.empty())
        {
            out << " # allowed: ";
            for(std::size_t i = 0; i < el.allowed_values.size(); ++i)
            {
                if(i != 0)
                    out << '|';
                out << el.allowed_values[i];
            }
        }
        out << '\n';
    }
    return {};
}

// Projects a resolved config into flat KEY=value lines: one line per resolved
// value, so a repeated path emits one line per value in order, with `key_prefix`
// prepended to every key. Keys are sorted by numeric ordinal so repeated
// instances round-trip in order (node[2] before node[10]) at N >= 11 rather than
// in lexicographic map order. A value carrying an embedded newline or carriage
// return is rejected before any write: the line-oriented flat grammar would let
// such a value forge an extra flag/var line on round-trip.
inline expected<void, error> emit_flat_document(const config &config, std::ostream &out,
                               std::string_view key_prefix,
                               std::string_view key_separator = "/",
                               std::string_view anchor = {})
{
    std::vector<std::string> sorted_keys = config.keys();
    std::stable_sort(sorted_keys.begin(), sorted_keys.end(),
        [](const std::string &a, const std::string &b) {
            return ordinal_sort_key(a) < ordinal_sort_key(b);
        });

    for(const std::string &key : sorted_keys)
    {
        std::string_view rendered = key;
        if(!strip_flat_anchor(rendered, anchor))
            continue;
        for(const std::string &value : config.get_all(key))
            if(value.find('\n') != std::string::npos
               || value.find('\r') != std::string::npos)
                return unexpected(error{errc::malformed_source, nucleus::format(
                    "flat emit: value for key '{}' carries an embedded newline or "
                    "carriage return, which the line-oriented flat format cannot "
                    "represent without forging an extra line", key)});
    }

    for(const std::string &key : sorted_keys)
    {
        std::string_view rendered = key;
        if(!strip_flat_anchor(rendered, anchor))
            continue;
        for(const std::string &value : config.get_all(key))
            out << key_prefix << render_flat_key(rendered, key_separator) << '='
                << value << '\n';
    }
    return {};
}

}

#endif

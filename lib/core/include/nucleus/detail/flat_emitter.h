#ifndef HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H
#define HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H

#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include <span>
#include <string>
#include <cstddef>
#include <ostream>
#include <string_view>

namespace nucleus::detail {

// A flat source has no nesting: the anchor path becomes the key. An element is a
// leaf iff no other declared element is anchored beneath it; container elements
// carry no value of their own and so produce no line.
[[nodiscard]] inline bool is_flat_leaf(const schema_element &el,
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
[[nodiscard]] inline bool strip_flat_anchor(std::string_view &key,
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
[[nodiscard]] inline std::string render_flat_key(std::string_view key,
                                                 std::string_view key_separator)
{
    std::string out;
    out.reserve(key.size());
    for(char c : key)
    {
        if(c == key_path::separator)
            out.append(key_separator);
        else
            out.push_back(c);
    }
    return out;
}

// Projects the declared schema into flat KEY= template lines: one line per declared
// LEAF path (its path joined by `key_separator`), blank value (template only), with
// `key_prefix` prepended to every key. A constrained leaf annotates its allowed
// set as a trailing `# allowed: a|b|c`.
inline void emit_flat_template(const configuration_space &space, std::ostream &out,
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
}

// Projects a resolved configuration into flat KEY=value lines: one line per
// resolved value, so a repeated path emits one line per value in order, with
// `key_prefix` prepended to every key. The flat line contract carries no embedded
// newline; values are written verbatim otherwise.
inline void emit_flat_document(const configuration &config, std::ostream &out,
                               std::string_view key_prefix,
                               std::string_view key_separator = "/",
                               std::string_view anchor = {})
{
    for(const std::string &key : config.keys())
    {
        std::string_view rendered = key;
        if(!strip_flat_anchor(rendered, anchor))
            continue;
        for(const std::string &value : config.get_all(key))
            out << key_prefix << render_flat_key(rendered, key_separator) << '='
                << value << '\n';
    }
}

}

#endif

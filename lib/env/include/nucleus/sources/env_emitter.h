#ifndef HPP_GUARD_NUCLEUS_SOURCES_ENV_EMITTER_H
#define HPP_GUARD_NUCLEUS_SOURCES_ENV_EMITTER_H

#include "nucleus/configuration_space.h"

#include "nucleus/schema/schema.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/keyspace/key_path.h"

#include <span>
#include <string>
#include <vector>
#include <ostream>

namespace nucleus::env {

namespace env_emitter_detail {

// A flat source has no nesting: the anchor path becomes the key. An element is a
// leaf iff no other declared element is anchored beneath it; container elements
// carry no value of their own and so produce no KEY= line.
[[nodiscard]] inline bool is_leaf(const schema_element &el,
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

}

// Projects the declared schema into flat KEY= template lines: one line per declared
// LEAF path (rendered as its '/'-joined path), blank value (template only). A
// constrained leaf annotates its allowed set as a trailing `# allowed: a|b|c`.
inline void emit_template(const configuration_space &space, std::ostream &out)
{
    const std::span<const schema_element> elements = space.schema_elements();
    for(const schema_element &el : elements)
    {
        if(!env_emitter_detail::is_leaf(el, elements))
            continue;
        out << el.declared_path().str() << '=';
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

// Projects a resolved configuration into flat KEY=value lines: one line per resolved
// value, so a repeated path emits one line per value in order. The flat KEY=value
// contract carries no embedded newline (consistent with env honesty); values are
// written verbatim otherwise.
inline void emit_document(const configuration &config, std::ostream &out)
{
    for(const std::string &key : config.keys())
        for(const std::string &value : config.get_all(key))
            out << key << '=' << value << '\n';
}

// The stateless emitter modeling nucleus::config_emitter: its members forward to the
// free functions above, so env satisfies the output contract by type as well.
struct emitter
{
    void emit_template(const configuration_space &space, std::ostream &out) const
    {
        nucleus::env::emit_template(space, out);
    }
    void emit_document(const configuration &config, std::ostream &out) const
    {
        nucleus::env::emit_document(config, out);
    }
};

}

#endif

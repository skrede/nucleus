#ifndef HPP_GUARD_NUCLEUS_RESOLVE_TYPED_CONVERSION_H
#define HPP_GUARD_NUCLEUS_RESOLVE_TYPED_CONVERSION_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"

#include <any>
#include <map>
#include <string>
#include <utility>

namespace nucleus {

// The typed conversion pass. It BORROWS the keyspace it reads text out of, the
// provenance it names the offending layer from, the schema whose elements
// declare the types, the converter registry that supplies the fallback
// converter, and the typed map it fills; it owns none of them.
class typed_conversion
{
public:
    typed_conversion(const keyspace &building, const provenance &prov,
                     const schema_registry &schema,
                     const converter_registry &converters,
                     std::map<std::string, std::any> &typed) noexcept
        : m_building(building)
        , m_provenance(prov)
        , m_schema(schema)
        , m_converters(converters)
        , m_typed(typed)
    {
    }

    // For every typed schema element, resolves the effective converter and
    // converts the corresponding paths in the building keyspace. Repeated
    // elements store per-instance typed values keyed by indexed path (e.g.
    // "cluster/node[0]/port"). Runs after validate() and before freeze().
    expected<void, resolve_fold_error> convert()
    {
        for(const schema_element &el : m_schema.elements())
        {
            const converter_registry::converter *conv = converter_for(el);
            if(conv == nullptr)
                continue;
            if(auto done = convert_element(el.declared_path().str(), *conv); !done)
                return done;
        }
        return {};
    }

private:
    // The element's own converter wins; a typed element carrying none falls back
    // to the registry. Absent for an untyped element, and for a typed one no
    // converter covers at all.
    const converter_registry::converter *
    converter_for(const schema_element &el) const
    {
        if(!el.type_identity.has_value())
            return nullptr;
        return el.converter ? &el.converter
                            : m_converters.find(el.type_identity.value());
    }

    // For both repeated elements and non-repeated elements that live under a
    // repeated container, every indexed path whose canonical form matches the
    // declared path is converted independently. A non-repeated leaf under a
    // repeated container (cluster/node/port when node is repeated) has no scalar
    // at the plain declared path at all; its values live at cluster/node[0]/port,
    // cluster/node[1]/port and so on.
    expected<void, resolve_fold_error>
    convert_element(const std::string &path_str,
                    const converter_registry::converter &conv)
    {
        bool found_any_indexed = false;
        for(const key_path &kp : m_building.paths())
        {
            if(m_schema.canonical_text(kp) != path_str || kp.str() == path_str)
                continue;
            found_any_indexed = true;
            if(auto one = convert_at(kp, kp.str(), conv); !one)
                return one;
        }
        if(found_any_indexed)
            return {};

        const auto kp_opt = key_path::parse(path_str);
        if(!kp_opt.has_value())
            return {};
        return convert_at(kp_opt.value(), path_str, conv);
    }

    expected<void, resolve_fold_error>
    convert_at(const key_path &kp, const std::string &path_str,
               const converter_registry::converter &conv)
    {
        const value *v = m_building.find(kp);
        if(v == nullptr)
            return {};
        auto res = conv(v->text());
        if(!res)
            return unexpected(conversion_failure(path_str, res.error()));
        m_typed.emplace(path_str, std::move(res).value());
        return {};
    }

    // A bad value names the layer it came from, so the report points at the file
    // to edit rather than at the path alone.
    resolve_fold_error conversion_failure(const std::string &path_str,
                                          const std::string &reason) const
    {
        std::string layer_label = "unknown layer";
        const origin *orig = m_provenance.of(path_str);
        if(orig != nullptr)
            layer_label = orig->layer;
        return error{errc::failed_conversion, nucleus::format(
            "conversion failed for '{}': {} (layer: {})",
            path_str, reason, layer_label)};
    }

    const keyspace                  &m_building;
    const provenance                &m_provenance;
    const schema_registry           &m_schema;
    const converter_registry        &m_converters;
    std::map<std::string, std::any> &m_typed;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_RESOLVE_CLI_ORDINAL_H
#define HPP_GUARD_NUCLEUS_RESOLVE_CLI_ORDINAL_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"

#include <set>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <algorithm>
#include <string_view>

namespace nucleus {

// Owns the plain-ordinal CLI overrides deferred during the fold. A bare decimal
// segment under a repeated container cannot be range-checked while folding: the
// instances it addresses only exist once the keyed merge and the strain slice
// have run. defer() rebrackets the path and parks the write; apply() runs after
// slice(), against the fully materialized keyspace. The keyspace, provenance and
// schema are borrowed, never owned, and must outlive this collaborator, which
// lives exactly as long as the resolve that holds it.
class cli_ordinal
{
    // Accumulates one path's rebracketing, carrying the borrowed inputs the
    // per-segment step needs so each step keeps a one-line signature.
    struct cli_rebracket_state
    {
        const std::set<std::string> &repeated;
        std::string_view label;
        std::string_view source_path;
        std::vector<std::string> segments;
        std::optional<std::size_t> ordinal;
        std::string prefix;
    };

    struct pending_cli_ordinal
    {
        std::size_t     ordinal;
        std::string     container_prefix;
        key_path        rebracketed;
        value           val;
        origin          prov;
    };

public:
    cli_ordinal(keyspace &building, provenance &prov,
                const schema_registry &schema) noexcept
        : m_building(building)
        , m_provenance(prov)
        , m_schema(schema)
    {
    }

    void reset() noexcept { m_deferred_cli_overrides.clear(); }

    // Parks the write and returns true when the path named a plain CLI ordinal,
    // so the fold skips the entry; false leaves the entry to the normal path.
    expected<bool, resolve_fold_error> defer(
        const key_path &path, const std::set<std::string> &repeated,
        const keyspace_entry &entry, std::string &expanded,
        const layered_handle &layer)
    {
        cli_rebracket_state state{repeated, layer.label, entry.path, {}, {}, {}};
        auto rebracketed = rebracket_cli_ordinals(state, path);
        if(!rebracketed)
            return unexpected(rebracketed.error());
        if(!state.ordinal)
            return false;
        m_deferred_cli_overrides.push_back(
            {*state.ordinal, std::move(state.prefix),
             key_path{std::move(state.segments)}, value::owned(std::move(expanded)),
             origin{layer.rank, layer.label, layer.owner, layer.inheritance_layer}});
        return true;
    }

    expected<void, resolve_fold_error> apply()
    {
        for(pending_cli_ordinal &override : m_deferred_cli_overrides)
        {
            const std::set<std::size_t> existing = existing_ordinals(override);
            if(!existing.contains(override.ordinal))
                return unexpected(error{errc::schema_violation, nucleus::format(
                    "argv ordinal {} for '{}' is out of range: "
                    "{} instance(s) exist; out of range",
                    override.ordinal, override.container_prefix,
                    existing.size())});
            write_back(override);
        }
        return {};
    }

private:
    keyspace                &m_building;
    provenance              &m_provenance;
    const schema_registry   &m_schema;
    std::vector<pending_cli_ordinal> m_deferred_cli_overrides;

    expected<void, resolve_fold_error>
    rebracket_cli_ordinals(cli_rebracket_state &state, const key_path &path) const
    {
        for(const std::string &segment : path.segments())
        {
            auto rebracketed = rebracket_cli_segment(state, segment);
            if(!rebracketed)
                return unexpected(rebracketed.error());
        }
        return {};
    }

    expected<void, resolve_fold_error>
    rebracket_cli_segment(cli_rebracket_state &state, const std::string &segment) const
    {
        const bool decimal = !segment.empty() && std::ranges::all_of(segment,
            [](char c) { return c >= '0' && c <= '9'; });
        const key_path container{state.segments};
        if(state.segments.empty() || !decimal
           || !state.repeated.contains(m_schema.canonical_text(container)))
        {
            state.segments.push_back(segment);
            return {};
        }
        auto parsed = cli_ordinal_of(state, segment);
        if(!parsed)
            return unexpected(parsed.error());
        state.ordinal = parsed.value();
        state.prefix = container.str();
        state.segments.back() += "[" + std::to_string(*state.ordinal) + "]";
        return {};
    }

    static expected<std::size_t, resolve_fold_error>
    cli_ordinal_of(const cli_rebracket_state &state, std::string_view segment)
    {
        auto ordinal = key_path::ordinal_in_domain(segment);
        if(!ordinal)
            return unexpected(error{errc::malformed_source, nucleus::format(
                "source '{}': malformed key path '{}': CLI ordinal segment "
                "'{}' is above the maximum ordinal {}",
                state.label, state.source_path, segment, key_path::max_ordinal)});
        return static_cast<std::size_t>(*ordinal);
    }

    std::set<std::size_t> existing_ordinals(const pending_cli_ordinal &override) const
    {
        const std::string bracket_prefix = override.container_prefix + "[";
        std::set<std::size_t> ordinals;
        for(const key_path &bp : m_building.paths())
        {
            const std::string bps = bp.str();
            if(!bps.starts_with(bracket_prefix))
                continue;
            const auto lb = bps.find('[', override.container_prefix.size());
            const auto rb = bps.find(']', lb);
            if(lb == std::string::npos || rb == std::string::npos)
                continue;
            const std::string_view digits = std::string_view(bps).substr(lb + 1, rb - lb - 1);
            if(auto const slot = key_path::ordinal_in_domain(digits))
                ordinals.insert(static_cast<std::size_t>(*slot));
        }
        return ordinals;
    }

    void write_back(const pending_cli_ordinal &override)
    {
        const origin *prior = m_provenance.of(override.rebracketed.str());
        if(prior == nullptr || prior->rank <= override.prov.rank)
        {
            m_building.set(override.rebracketed, override.val);
            m_provenance.record(override.rebracketed.str(), override.prov);
        }
    }
};

}

#endif

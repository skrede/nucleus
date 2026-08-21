#ifndef HPP_GUARD_NUCLEUS_RESOLVE_FOLD_ENTRY_H
#define HPP_GUARD_NUCLEUS_RESOLVE_FOLD_ENTRY_H

#include "nucleus/resolve/layer_fold.h"
#include "nucleus/resolve/cli_ordinal.h"
#include "nucleus/resolve/keyed_divert.h"
#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/repeated_sweep.h"
#include "nucleus/resolve/repeated_placement.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/substitution_budget.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <string>
#include <cstddef>
#include <utility>

namespace nucleus {

// Folds one source entry into the building keyspace: expands its tokens, parses
// its path, offers it to each interceptor that may claim it for a later stage,
// and stores whatever no interceptor claimed. Every collaborator it consults is
// borrowed and outlives the resolve; the substitution budget is the one piece of
// per-load state it owns, because the ceiling is per load rather than per value.
//
// Unified storage: ALL repeated paths, leaves and containers alike, are stored as
// indexed scalars -- "config/tag" with duplicate entries becomes "config/tag[0]",
// "config/tag[1]" and so on, and "cluster/node[0]/port" from a document source is
// stored directly. No collection map is used.
class fold_entry
{
public:
    fold_entry(layer_fold &layers, repeated_sweep &sweep, cli_ordinal &cli,
               keyed_divert &divert, keyspace &building, provenance &prov,
               const schema_registry &schema, const tokenizer_registry &tokenizer,
               const tree_tokenizer_registry &tree_tokenizer) noexcept
        : m_cli(cli)
        , m_layers(layers)
        , m_building(building)
        , m_divert(divert)
        , m_provenance(prov)
        , m_tokenizer(tokenizer)
        , m_tree_tokenizer(tree_tokenizer)
        , m_placement(layers, sweep, schema)
    {
    }

    // A bounded-depth fanout spanning several values is charged against a single
    // running count, so the total-substitution ceiling holds across the load.
    void begin_fold(std::size_t expansion_budget) noexcept
    {
        m_budget = substitution_budget(expansion_budget);
    }

    expected<void, resolve_fold_error>
    fold_one(const keyspace_entry &entry, const layered_handle &layer)
    {
        auto expanded = expand(entry, layer);
        if(!expanded)
            return unexpected(expanded.error());
        auto parsed = parse_path(entry, layer);
        if(!parsed)
            return unexpected(parsed.error());
        auto claimed = intercept(parsed.value(), entry, expanded.value(), layer);
        if(!claimed)
            return unexpected(claimed.error());
        if(!claimed.value())
            store_plainly(parsed.value(), entry, expanded.value(), layer);
        return {};
    }

private:
    cli_ordinal                     &m_cli;
    layer_fold                      &m_layers;
    keyspace                        &m_building;
    keyed_divert                    &m_divert;
    provenance                      &m_provenance;
    const tokenizer_registry        &m_tokenizer;
    const tree_tokenizer_registry   &m_tree_tokenizer;
    substitution_budget m_budget;
    repeated_placement  m_placement;

    expected<std::string, resolve_fold_error>
    expand(const keyspace_entry &entry, const layered_handle &layer)
    {
        token_result expanded = layer.origin_file
            ? resolve_tokens(entry.value.text(), m_tokenizer, *layer.origin_file,
                             m_budget, &m_tree_tokenizer)
            : resolve_tokens(entry.value.text(), m_tokenizer, m_budget,
                             &m_tree_tokenizer);
        if(!expanded)
            return unexpected(error{errc::unresolved_token, nucleus::format(
                "source '{}': token resolution failed for key '{}': {}",
                layer.label, entry.path, expanded.error().message)});
        return std::move(expanded).value();
    }

    static expected<key_path, resolve_fold_error>
    parse_path(const keyspace_entry &entry, const layered_handle &layer)
    {
        auto parsed = key_path::parse(entry.path);
        if(!parsed)
            return unexpected(error{errc::malformed_source, nucleus::format(
                "source '{}': malformed key path '{}': {}",
                layer.label, entry.path, parsed.error())});
        return std::move(parsed).value();
    }

    // Each interceptor answers whether it claimed the entry; the first that does
    // ends the entry's journey here and may have moved out of `text`.
    expected<bool, resolve_fold_error>
    intercept(const key_path &path, const keyspace_entry &entry, std::string &text,
              const layered_handle &layer)
    {
        auto deferred =
            m_cli.defer(path, m_layers.repeated_containers(), entry, text, layer);
        if(!deferred || deferred.value())
            return deferred;
        auto diverted = m_divert.divert(path, entry, text, layer);
        if(!diverted || diverted.value())
            return diverted;
        return m_placement.place(path, entry, text, layer);
    }

    void store_plainly(const key_path &path, const keyspace_entry &entry,
                       std::string &text, const layered_handle &layer)
    {
        m_building.set(path, value::owned(std::move(text)));
        m_provenance.record(entry.path, origin{layer.rank, layer.label, layer.owner,
                                               layer.inheritance_layer});
    }
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RESOLUTION_CONTEXT_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RESOLUTION_CONTEXT_H

#include "nucleus/resolve/fold_entry.h"
#include "nucleus/resolve/layer_fold.h"
#include "nucleus/resolve/cli_ordinal.h"
#include "nucleus/resolve/schema_gate.h"
#include "nucleus/resolve/keyed_divert.h"
#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/repeated_sweep.h"
#include "nucleus/resolve/strain_slicing.h"
#include "nucleus/resolve/keyspace_values.h"
#include "nucleus/resolve/typed_conversion.h"
#include "nucleus/resolve/keyed_merge_state.h"
#include "nucleus/resolve/reference_resolution.h"
#include "nucleus/resolve/keyed_collection_merge.h"

#include "nucleus/config.h"
#include "nucleus/expected.h"
#include "nucleus/strain_scope.h"

#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"

#include "nucleus/config_source/inherit_declaration.h"

#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/substitution_budget.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <any>
#include <map>
#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>

namespace nucleus {

// The transient hand-off vehicle and the convergence keystone. It BORROWS the flat sibling
// registries it consults, owns none of them, and lives for one load()/resolve(). This is the ONLY
// path by which one registry reaches another: an operation takes the context (or a named sibling)
// as a parameter and never stores it. The source registry is not borrowed -- the sources to fold
// arrive in the precedence stack the facade hands to fold(). Beyond the registries it holds the
// transient working state of one resolve; freeze() copies the values out, then it is destroyed.
class resolution_context
{
public:
    using layered_handle = nucleus::layered_handle;

    resolution_context(const schema_registry &schema,
                        const tokenizer_registry &tokenizer,
                        const converter_registry &converters,
                        const tree_tokenizer_registry &tree_tokenizer) noexcept
        : m_schema(schema)
        , m_tokenizer(tokenizer)
        , m_converters(converters)
        , m_tree_tokenizer(tree_tokenizer)
        , m_layers(m_schema, m_buffers, m_dispositions)
        , m_sweep(m_building, m_provenance, m_schema)
        , m_cli_ordinal(m_building, m_provenance, m_schema)
        , m_conversion(m_building, m_provenance, m_schema, m_converters, m_typed)
        , m_merge(m_building, m_provenance, m_keyed)
        , m_references(m_building, m_tree_tokenizer)
        , m_gate(m_building, m_schema, m_sweep, m_keyed_satisfied,
                 [this] { return group_snapshot(); })
        , m_divert(m_keyed, m_schema)
        , m_slicing(m_building, m_provenance, m_schema, m_sweep, m_keyed,
                    m_dispositions, m_keyed_satisfied)
        , m_entry(m_layers, m_sweep, m_cli_ordinal, m_divert, m_building,
                  m_provenance, m_schema, m_tokenizer, m_tree_tokenizer)
    {
    }

    // Every sub-unit below is constructed from this object's own members and one of them captures `this`, so a copy or a move would leave the whole set driving the source.
    resolution_context(const resolution_context &) = delete;
    resolution_context &operator=(const resolution_context &) = delete;
    resolution_context(resolution_context &&) = delete;
    resolution_context &operator=(resolution_context &&) = delete;
    ~resolution_context() = default;

    // Borrowed by CONST reference and read-only, so concurrent loads on one shared
    // const config_space share nothing mutable and need no synchronization.
    const schema_registry &schema() const noexcept { return m_schema; }
    const tokenizer_registry &tokenizer() const noexcept { return m_tokenizer; }
    const converter_registry &converters() const noexcept { return m_converters; }

    // The caller assigns ascending ranks for cross-source precedence; low rank folds first and
    // equal ranks keep input order. Each handle is pulled once, so project->pull->inherit holds.
    expected<void, resolve_fold_error>
    fold(std::span<layered_handle> layers)
    {
        const std::vector<layered_handle *> ordered = m_layers.begin_fold(layers);
        if(auto built = m_keyed.build(m_schema); !built)
            return unexpected(built.error());
        m_cli_ordinal.reset();
        m_entry.begin_fold(m_expansion_budget);
        for(layered_handle *lh : ordered)
        {
            auto acquired = m_layers.acquire(*lh);
            if(!acquired)
                return unexpected(acquired.error());
            config_source_batch &batch = acquired.value();
            if(auto begun = m_layers.begin_layer(batch); !begun)
                return unexpected(begun.error());
            m_divert.reset();
            for(const keyspace_entry &entry : batch.entries)
                if(auto folded = m_entry.fold_one(entry, *lh); !folded)
                    return unexpected(folded.error());
            m_layers.end_layer(batch);
        }
        return {};
    }

    expected<void, resolve_fold_error> merge_keyed_collections() { return m_merge.merge(); }

    expected<void, resolve_fold_error>
    slice(const std::optional<std::string> &selection = std::nullopt,
          strain_scope_policy policy = strain_scope_policy::space_open_container_closed)
    {
        return m_slicing.slice(selection, policy);
    }

    expected<void, resolve_fold_error> apply_deferred_cli_overrides() { return m_cli_ordinal.apply(); }

    expected<void, resolve_fold_error> resolve_references() { return m_references.resolve(m_reference_budget); }

    expected<void, resolve_fold_error> validate() { return m_gate.validate(); }

    expected<void, resolve_fold_error> convert() { return m_conversion.convert(); }

    // Copies every building value OUT, paired with its provenance, into an immutable config.
    config freeze(std::vector<degradation> degraded = {}) const
    {
        return {owned_values(m_building), m_typed, m_provenance, std::move(degraded)};
    }

    // 0 maps to the engine default, so a budget is never capped at zero.
    void set_reference_budget(std::size_t budget) noexcept
    {
        if(budget != 0)
            m_reference_budget = budget;
    }

    // 0 maps to the engine default; must be set before fold(), where pass-1 expands.
    void set_expansion_budget(std::size_t budget) noexcept
    {
        if(budget != 0)
            m_expansion_budget = budget;
    }

private:
    // config's snapshot constructor is private with this class as its only friend.
    config group_snapshot() const { return {owned_values(m_building), m_provenance}; }

    const schema_registry           &m_schema;
    const tokenizer_registry        &m_tokenizer;
    const converter_registry        &m_converters;
    const tree_tokenizer_registry   &m_tree_tokenizer;

    std::size_t m_reference_budget = default_reference_budget;
    std::size_t m_expansion_budget = default_expansion_budget;

    keyspace m_building;
    provenance m_provenance;
    // Typed values populated by convert(), keyed by full indexed path.
    std::map<std::string, std::any> m_typed;
    std::vector<retained_buffer> m_buffers;
    // Containers whose single primary-keyed instance was sliced onto the unified hierarchy --
    // evidence for the enforcer that their identity is satisfied even though the key field
    // was consumed and never appears as a leaf.
    std::vector<std::string> m_keyed_satisfied;
    // Re-open dispositions collected from all source batches during fold().
    std::vector<extend_disposition> m_dispositions;

    layer_fold m_layers;
    repeated_sweep m_sweep;
    cli_ordinal m_cli_ordinal;
    keyed_merge_state m_keyed;
    typed_conversion m_conversion;
    keyed_collection_merge m_merge;
    reference_resolution m_references;
    schema_gate m_gate;
    keyed_divert m_divert;
    strain_slicing m_slicing;
    fold_entry m_entry;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_ENTRY_RESOLUTION_CONTEXT_H
#define HPP_GUARD_NUCLEUS_ENTRY_RESOLUTION_CONTEXT_H

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/configuration.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/source/source_registry.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"
#include "nucleus/diagnostics/key_suggester.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/token_resolution.h"

#include "nucleus/result.h"
#include "nucleus/format.h"

#include <map>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus {

// The error a resolve fold can report: a source pull failure or a token
// resolution failure, surfaced verbatim with the offending layer named.
using resolve_fold_error = std::string;

// The transient hand-off vehicle and the convergence keystone. It BORROWS (holds
// references to) the three flat sibling registries the facade owns; it does not
// own them and lives only for the duration of one load()/resolve(). This is the
// ONLY path by which one registry reaches another: a registry operation takes the
// context (or a specific sibling) as a parameter and never stores it. Living in
// entry/ -- the one place that knows all three registries by type -- physically
// reinforces that no registry owns another.
//
// Beyond the borrowed registries it holds the transient working state of one
// resolve: the building keyspace, the provenance map, and the retained source
// buffers that pin every view-value until copy-out. None of this outlives the
// call; freeze() copies values out into a self-owning configuration and the
// context (and with it every buffer) is destroyed.
class resolution_context
{
public:
    resolution_context(schema_registry &schema,
                        tokenizer_registry &tokenizer,
                        source_registry &sources) noexcept
        : m_schema(schema), m_tokenizer(tokenizer), m_sources(sources)
    {
    }

    [[nodiscard]] schema_registry &schema() noexcept { return m_schema; }
    [[nodiscard]] tokenizer_registry &tokenizer() noexcept { return m_tokenizer; }
    [[nodiscard]] source_registry &sources() noexcept { return m_sources; }

    [[nodiscard]] keyspace &building() noexcept { return m_building; }
    [[nodiscard]] provenance &origins() noexcept { return m_provenance; }

    // The single fold: layer the stack into the building keyspace by precedence,
    // recording each winning value's provenance in the SAME step so the two can
    // never diverge. Expand-then-layer: every value's ${...} tokens are resolved
    // per-source at read time (against the borrowed tokenizer registry) BEFORE it
    // is layered, so layering operates on already-resolved strings.
    //
    // Layers are visited lowest rank first; a key set by a higher (or equal, by
    // arrival) rank overwrites a lower one (last-writer-wins within rank). Every
    // batch's retained buffer is pinned in this context until freeze() copies the
    // values out, so no view dangles mid-fold.
    [[nodiscard]] result<std::monostate, resolve_fold_error> fold(const source_stack &stack)
    {
        std::vector<const source_layer *> ordered;
        ordered.reserve(stack.layers().size());
        for(const source_layer &layer : stack.layers())
            ordered.push_back(&layer);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const source_layer *a, const source_layer *b) {
                             return a->rank < b->rank;
                         });

        for(const source_layer *layer : ordered)
        {
            if(layer->src == nullptr)
                continue;

            source_result pulled = layer->src->pull();
            if(!pulled)
                return fail(::nucleus::format("source '{}': {}",
                                              layer->label, pulled.error()));

            source_batch &batch = pulled.value();

            for(keyspace_entry &entry : batch.entries)
            {
                token_result expanded = resolve_tokens(entry.value.text(), m_tokenizer);
                if(!expanded)
                    return fail(::nucleus::format(
                        "source '{}': token resolution failed for key '{}': {}",
                        layer->label, entry.path, expanded.error().message));

                auto path = key_path::parse(entry.path);
                if(!path)
                    continue;

                // Value and provenance written together: they cannot diverge.
                m_building.set(path.value(), value::owned(std::move(expanded).value()));
                m_provenance.record(entry.path,
                                    origin{layer->rank, layer->label, layer->owner});
            }

            // Pin the batch's buffer so its (now copied-out) views stayed valid
            // through the expansion above; it is released only at context destruction.
            m_buffers.push_back(std::move(batch.buffer));
        }

        return std::monostate{};
    }

    // Validates the folded keyspace against the borrowed schema -- the step that
    // makes the schema authoritative over CONTENT at resolve time, reached only
    // through ctx.schema() so the registry stays a borrowed sibling. Runs ONLY
    // when the schema declares a surface: a host that registers no schema gets no
    // content gate (an empty schema is not a claim that nothing is allowed). An
    // undeclared key is reported with its nearest declared neighbor so a typo is
    // actionable; missing required/identity fields are reported by the enforcer.
    [[nodiscard]] result<std::monostate, resolve_fold_error> validate()
    {
        if(m_schema.surface().empty())
            return std::monostate{};

        schema_validation checked = schema_enforcer::validate(m_schema, m_building);
        if(checked)
            return std::monostate{};

        const std::vector<key_path> surface = m_schema.surface();
        std::vector<std::string> known;
        known.reserve(surface.size());
        for(const key_path &path : surface)
            known.push_back(path.str());

        std::string report = "schema validation failed:";
        for(const schema_violation &v : checked.error())
        {
            report += ::nucleus::format("\n  - {}", v.reason);
            if(!m_schema.recognizes_text(v.path))
            {
                auto near = suggest_keys(v.path, known, 1);
                if(!near.empty())
                    report += ::nucleus::format(" (did you mean '{}'?)", near.front());
            }
        }
        return fail(std::move(report));
    }

    // Copies every building value OUT into an owned snapshot and pairs it with the
    // provenance recorded alongside it, producing the immutable, self-owning
    // configuration. After this returns the context (and every retained buffer)
    // may be dropped: the configuration holds no view into any of them.
    [[nodiscard]] configuration freeze() const
    {
        std::map<std::string, std::string> owned;
        for(const key_path &path : m_building.paths())
        {
            if(const value *v = m_building.find(path))
                owned.emplace(path.str(), std::string(v->text()));
        }
        return configuration(std::move(owned), m_provenance);
    }

private:
    schema_registry &m_schema;
    tokenizer_registry &m_tokenizer;
    source_registry &m_sources;

    keyspace m_building;
    provenance m_provenance;
    std::vector<retained_buffer> m_buffers;
};

}

#endif

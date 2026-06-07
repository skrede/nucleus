#ifndef HPP_GUARD_NUCLEUS_ENTRY_RESOLUTION_CONTEXT_H
#define HPP_GUARD_NUCLEUS_ENTRY_RESOLUTION_CONTEXT_H

#include "nucleus/format.h"
#include "nucleus/result.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/strain_scope.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <map>
#include <limits>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <algorithm>

namespace nucleus {

// The error a resolve fold can report: a source pull failure or a token
// resolution failure, surfaced verbatim with the offending layer named.
using resolve_fold_error = std::string;

// The transient hand-off vehicle and the convergence keystone. It BORROWS (holds
// references to) the flat sibling registries it actually consults during a
// resolve; it does not own them and lives only for the duration of one
// load()/resolve(). This is the ONLY path by which one registry reaches another:
// a registry operation takes the context (or a specific sibling) as a parameter
// and never stores it. Living in entry/ -- the one place that may name the
// registries by type -- physically reinforces that no registry owns another.
//
// It borrows the schema (read by validate() to gate the resolved keyspace) and
// the tokenizer registry (read by the fold to expand ${...} per source). It does
// NOT borrow the source registry: in v0.1 the sources to fold arrive directly in
// the precedence stack the facade passes to fold(), and the source registry holds
// only name stubs with no pullable source to consult. The keystone borrows only
// what it genuinely reads rather than claiming a sibling it never touches.
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
                        tokenizer_registry &tokenizer) noexcept
        : m_schema(schema), m_tokenizer(tokenizer)
    {
    }

    [[nodiscard]] schema_registry &schema() noexcept { return m_schema; }
    [[nodiscard]] tokenizer_registry &tokenizer() noexcept { return m_tokenizer; }

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

        // The schema-derived projection every source is offered before it pulls,
        // so a document source renders repeatable keyed containers faithfully.
        // Built once from the borrowed schema; flat sources ignore it.
        const schema_projection projection = m_schema.projection();

        for(const source_layer *layer : ordered)
        {
            if(layer->src == nullptr)
                continue;

            layer->src->apply_projection(projection);
            source_result pulled = layer->src->pull();
            if(!pulled)
                return fail(nucleus::format("source '{}': {}",
                                              layer->label, pulled.error()));

            source_batch &batch = pulled.value();

            for(keyspace_entry &entry : batch.entries)
            {
                token_result expanded = resolve_tokens(entry.value.text(), m_tokenizer);
                if(!expanded)
                    return fail(nucleus::format(
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

    // Collapses keyed-container instances into the ONE unified hierarchy the
    // resolved configuration promises. A primary-key value is internal to
    // resolution -- a transient path segment keeping instances distinct through
    // the fold -- and must NEVER survive into the frozen keyspace: a key value
    // as a resolved segment would make the tree untraversable without knowing
    // the key. Run between fold and validate.
    //
    // With a selection: the matching named strain survives, non-matching named
    // strains are pruned from the keyspace, and the selected strain's entries
    // are re-laid onto the declared (stripped) paths. Selecting a value that
    // matches no strain is a loud error listing every available strain value --
    // including when the container holds no keyed instances at all. Selecting
    // when the schema declares no primary key is a loud error.
    //
    // Without a selection: exactly one named strain auto-resolves (its entries
    // re-laid); several named strains with no selection is a loud error naming
    // the container and every strain; anonymous-only content collapses unchanged.
    //
    // The scope policy applies whenever a strain resolves -- explicitly selected
    // or auto-resolved -- so the two paths cannot diverge for the same strain.
    [[nodiscard]] result<std::monostate, resolve_fold_error>
    slice(const std::optional<std::string> &selection = std::nullopt,
          strain_scope_policy policy = strain_scope_policy::space_open_container_closed)
    {
        // When the caller supplies a selection, the schema must declare a
        // primary key: without one there is no slice selector at all. Check
        // before the per-element loop because the loop body is only entered
        // for identity elements -- if there are none it would never fire.
        if(selection.has_value())
        {
            bool has_identity = false;
            for(const schema_element &any : m_schema.elements())
            {
                if(any.identity) { has_identity = true; break; }
            }
            if(!has_identity)
                return fail(nucleus::format(
                    "selection '{}' cannot be applied: the schema declares "
                    "no primary key",
                    selection.value()));
        }

        for(const schema_element &el : m_schema.elements())
        {
            if(!el.identity)
                continue;

            const key_path container = el.container();

            // Bucket every keyed leaf by its instance's key value. paths() is a
            // snapshot, so mutating the keyspace below is safe. A key value that
            // shadows a declared element name can never be bucketed -- report it
            // loudly here instead of letting validation fail later with an
            // unrelated unknown-key suggestion.
            std::map<std::string, std::vector<key_path>> strains;
            for(const key_path &path : m_building.paths())
            {
                if(m_schema.keyed_instance_path(container, path))
                    strains[path.segments()[container.size()]].push_back(path);
                else if(m_schema.key_value_collision(container, path))
                    return fail(nucleus::format(
                        "primary-key value '{}' in container '{}' collides with "
                        "a declared element of the same name: a strain cannot "
                        "be keyed by a sibling element's name",
                        path.segments()[container.size()], container.str()));
            }

            if(strains.empty())
            {
                // A selection against a container holding no keyed instances is
                // unsatisfiable and must fail loudly, never silently resolve to
                // whatever template content exists.
                if(selection.has_value())
                    return fail(nucleus::format(
                        "selection '{}' does not match any strain in container "
                        "'{}': the container holds no primary-keyed instances",
                        selection.value(), container.str()));
                continue;
            }

            // Resolve WHICH strain survives: the explicit selection, or the
            // single named strain present. Several named strains with no
            // selection is the undefined resolve the model rejects.
            std::string chosen;
            if(selection.has_value())
            {
                // If the requested value is not among the bucketed strains,
                // fail loudly listing every available value.
                if(strains.find(selection.value()) == strains.end())
                {
                    std::string available;
                    for(const auto &[key_value, _] : strains)
                    {
                        if(!available.empty())
                            available += ", ";
                        available += nucleus::format("'{}'", key_value);
                    }
                    return fail(nucleus::format(
                        "selection '{}' does not match any strain in container "
                        "'{}'; available: {}",
                        selection.value(), container.str(), available));
                }
                chosen = selection.value();
            }
            else if(strains.size() > 1)
            {
                std::string keys;
                for(const auto &[key_value, _] : strains)
                {
                    if(!keys.empty())
                        keys += ", ";
                    keys += nucleus::format("'{}'", key_value);
                }
                return fail(nucleus::format(
                    "container '{}' holds {} primary-keyed instances ({}) and "
                    "no instance is selected: a configuration space resolves "
                    "exactly one",
                    container.str(), strains.size(), keys));
            }
            else
                chosen = strains.begin()->first;

            // Ld: the chosen strain's defining layer -- the minimum
            // first-introduction rank among its keyed entries. A later overwrite
            // of an entry does not move the defining layer. No recorded rank for
            // any entry is an invariant violation (the fold records provenance
            // with every set), never a silent default.
            std::size_t Ld = 0;
            {
                bool found_any = false;
                for(const key_path &keyed : strains.at(chosen))
                {
                    const std::size_t *first = m_provenance.first_rank_of(keyed.str());
                    if(first != nullptr && (!found_any || *first < Ld))
                    {
                        Ld = *first;
                        found_any = true;
                    }
                }
                if(!found_any)
                    return fail(nucleus::format(
                        "strain '{}' in container '{}' has no recorded "
                        "provenance: resolve cannot bound its defining layer",
                        chosen, container.str()));
            }

            // Ls: the first layer ABOVE the defining layer that introduces a
            // competing named strain. A competitor introduced at or below Ld is
            // not the "next" strain and never bounds the chosen one. Unbounded
            // (max size_t) when no competitor is introduced above Ld.
            std::size_t Ls = std::numeric_limits<std::size_t>::max();
            for(const auto &[key_value, paths] : strains)
            {
                if(key_value == chosen)
                    continue;
                for(const key_path &keyed : paths)
                {
                    const std::size_t *first = m_provenance.first_rank_of(keyed.str());
                    if(first != nullptr && *first > Ld && *first < Ls)
                        Ls = *first;
                }
            }

            // Prune every non-chosen named strain: remove its keyed paths from
            // the building keyspace and forget their provenance.
            for(auto &[key_value, paths] : strains)
            {
                if(key_value == chosen)
                    continue;
                for(const key_path &keyed : paths)
                {
                    m_building.remove(keyed);
                    m_provenance.forget(keyed.str());
                }
            }

            // file_level general pre-pass: prune ALL paths whose winning rank
            // exceeds Ld. This sweeps keyed and general entries alike, and runs
            // on a snapshot to avoid iterator invalidation during removal.
            if(policy == strain_scope_policy::file_level)
            {
                const std::vector<key_path> snapshot = m_building.paths();
                for(const key_path &path : snapshot)
                {
                    const origin *orig = m_provenance.of(path.str());
                    if(orig != nullptr && orig->rank > Ld)
                    {
                        m_building.remove(path);
                        m_provenance.forget(path.str());
                    }
                }
            }

            relay_strain(strains.at(chosen), policy, Ld, Ls);

            // The strain's key value named the instance and was consumed; the
            // enforcer's identity-presence check is satisfied structurally.
            m_keyed_satisfied.push_back(container.str());
        }

        return std::monostate{};
    }

    // Validates the folded keyspace against the borrowed schema -- the step that
    // makes the schema authoritative over CONTENT at resolve time, reached only
    // through ctx.schema() so the registry stays a borrowed sibling. Runs ONLY
    // when the schema declares a surface: a host that registers no schema gets no
    // content gate (an empty schema is not a claim that nothing is allowed). An
    // undeclared key is reported with its nearest declared neighbor so a typo is
    // actionable; missing required fields are reported by the enforcer.
    [[nodiscard]] result<std::monostate, resolve_fold_error> validate()
    {
        if(m_schema.surface().empty())
            return std::monostate{};

        schema_validation checked = schema_enforcer::validate(m_schema, m_building,
                                                              m_keyed_satisfied);
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
            report += nucleus::format("\n  - {}", v.reason);
            if(!m_schema.recognizes_text(v.path))
            {
                auto near = suggest_keys(v.path, known, 1);
                if(!near.empty())
                    report += nucleus::format(" (did you mean '{}'?)", near.front());
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
    // Re-lays one strain's keyed entries onto the unified (key-stripped) paths,
    // applying the policy's rank-bounded filter to each entry's WINNING rank:
    // file_level and space_open_container_closed freeze the strain's keyed
    // entries at the defining layer Ld; container_open_until_next_strain admits
    // them up to but excluding Ls. An entry already at the unified path with a
    // higher winning rank (a flat override such as argv) displaces the keyed
    // value -- it composes by plain rank precedence, outside the bounds.
    void relay_strain(const std::vector<key_path> &keyed_paths,
                      strain_scope_policy policy, std::size_t Ld, std::size_t Ls)
    {
        for(const key_path &keyed : keyed_paths)
        {
            const origin *from = m_provenance.of(keyed.str());
            const std::size_t entry_rank = from != nullptr ? from->rank : 0;

            const bool excluded =
                policy == strain_scope_policy::container_open_until_next_strain
                    ? entry_rank >= Ls
                    : entry_rank > Ld;
            if(excluded)
            {
                m_building.remove(keyed);
                m_provenance.forget(keyed.str());
                continue;
            }

            auto unified = key_path::parse(m_schema.canonical_text(keyed));
            if(!unified)
                continue;

            const origin *at = m_provenance.of(unified.value().str());
            const bool displaced = from != nullptr && at != nullptr
                                   && at->rank > from->rank;
            if(!displaced)
            {
                if(const value *v = m_building.find(keyed))
                {
                    m_building.set(unified.value(), *v);
                    if(from != nullptr)
                        m_provenance.record(unified.value().str(), *from);
                }
            }
            m_building.remove(keyed);
            m_provenance.forget(keyed.str());
        }
    }

    schema_registry &m_schema;
    tokenizer_registry &m_tokenizer;

    keyspace m_building;
    provenance m_provenance;
    std::vector<retained_buffer> m_buffers;
    // Containers whose single primary-keyed instance was sliced onto the unified
    // hierarchy -- evidence for the enforcer that their identity is satisfied
    // even though the key field was consumed and never appears as a leaf.
    std::vector<std::string> m_keyed_satisfied;
};

}

#endif

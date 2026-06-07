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

#include "nucleus/source/inherit_declaration.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <any>
#include <map>
#include <set>
#include <limits>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <typeindex>
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

        // Build a set of repeated-path strings from the schema once, so the
        // inner entry loop classifies each path in O(log n) without rescanning.
        std::set<std::string> repeated_paths;
        for(const schema_element &el : m_schema.elements())
        {
            if(el.repeated)
                repeated_paths.insert(el.declared_path().str());
        }

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

            // Tracks which repeated paths have already received their first entry
            // from this layer (triggering replace). Reset per layer so a new layer
            // always starts fresh with a replace before appending.
            std::set<std::string> seen_repeated_this_layer;

            for(keyspace_entry &entry : batch.entries)
            {
                // Expand tokens FIRST, before the repeated-path branch, so each
                // collection value is independently expanded before accumulation.
                token_result expanded = resolve_tokens(entry.value.text(), m_tokenizer);
                if(!expanded)
                    return fail(nucleus::format(
                        "source '{}': token resolution failed for key '{}': {}",
                        layer->label, entry.path, expanded.error().message));

                auto path = key_path::parse(entry.path);
                if(!path)
                    continue;

                // Use the canonical (key-stripped) path to classify the entry as
                // repeated. A repeated leaf under a keyed container arrives from
                // a document source with a transient instance segment inserted
                // (e.g. cluster/server/yin/tags rather than cluster/server/tags);
                // the canonical form matches the declared repeated path.
                const std::string canonical_path = m_schema.canonical_text(path.value());
                if(repeated_paths.count(canonical_path))
                {
                    // Capability gate: a source without duplicate_keys cannot
                    // legally produce more than one entry for the same repeated
                    // path per layer. If it somehow does, that is a source-level
                    // bug -- fail loudly.
                    if(!entry.capabilities.supports(capability::duplicate_keys)
                       && seen_repeated_this_layer.count(entry.path) != 0)
                    {
                        return fail(nucleus::format(
                            "source '{}': repeated field '{}' received multiple "
                            "values from a source that does not support "
                            "duplicate_keys; a flat source can supply at most one "
                            "value per repeated field per layer",
                            layer->label, entry.path));
                    }

                    const bool is_first_in_layer =
                        (seen_repeated_this_layer.count(entry.path) == 0);
                    seen_repeated_this_layer.insert(entry.path);

                    if(is_first_in_layer)
                    {
                        // First entry from this layer: REPLACE (clears any
                        // lower-layer collection).
                        std::vector<value> init;
                        init.push_back(value::owned(std::move(expanded).value()));
                        m_building.replace_collection(path.value(), std::move(init));
                        // Provenance: start a fresh per-element origins vector.
                        std::vector<origin> col_origins;
                        col_origins.push_back(
                            origin{layer->rank, layer->label, layer->owner});
                        m_provenance.record_collection(path.value().str(),
                                                       std::move(col_origins));
                    }
                    else
                    {
                        // Subsequent entry from the same layer: APPEND.
                        m_building.append(path.value(),
                                          value::owned(std::move(expanded).value()));
                        // Append this element's origin to the existing vector.
                        const std::vector<origin> *existing =
                            m_provenance.collection_origins_of(path.value().str());
                        std::vector<origin> updated =
                            existing ? *existing : std::vector<origin>{};
                        updated.push_back(
                            origin{layer->rank, layer->label, layer->owner});
                        m_provenance.record_collection(path.value().str(),
                                                       std::move(updated));
                    }
                }
                else
                {
                    // Single-value path: unchanged last-write-wins behavior.
                    // Value and provenance written together; they cannot diverge.
                    m_building.set(path.value(), value::owned(std::move(expanded).value()));
                    m_provenance.record(entry.path,
                                        origin{layer->rank, layer->label, layer->owner});
                }
            }

            // Extract extend dispositions declared by this batch (empty for flat
            // sources; populated by document sources that parse extend= attributes).
            for(const extend_disposition &d : batch.dispositions)
                m_dispositions.push_back(d);

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

            // Build a disposition index for fast lookup in the checks
            // below and in the relay_strain call.
            std::map<std::pair<std::string, std::string>, extend_strength> disp_index;
            for(const extend_disposition &d : m_dispositions)
                disp_index[{d.container_path, d.key_value}] = d.strength;

            // Cross-layer re-open and extend-without-base checks.
            // These checks apply only to document-band sources (rank >= base, i.e.
            // inheritance chain layers). Flat source layering (env, argv, defaults)
            // contributes to strains by design and is never a re-open error.
            // For each named strain: compute the set of distinct document-band ranks
            // at which entries were laid down, combining first-introduction ranks
            // with winning ranks. An entry overwritten by a higher-rank layer has its
            // first-introduction rank (the base layer) AND its winning rank (the
            // deriving layer) both recorded -- so a chain re-open via overwrite is
            // correctly detected as multi-layer even when the overwrite collapses the
            // building keyspace to a single path. A strain present at more than one
            // document-band layer without an extend disposition is a re-open error.
            // A strain with an extend disposition but entries at only one document-
            // band layer has no base (extend without base).
            static constexpr std::size_t doc_band_min =
                static_cast<std::size_t>(layer_rank::base);
            for(const auto &[key_value, keyed_paths] : strains)
            {
                std::set<std::size_t> intro_ranks;
                for(const key_path &kp : keyed_paths)
                {
                    const std::size_t *first = m_provenance.first_rank_of(kp.str());
                    if(first != nullptr && *first >= doc_band_min)
                        intro_ranks.insert(*first);
                    // Also include the winning rank: an overwrite by a higher-rank
                    // layer means the winner and the first-introducer differ, and
                    // both layers must be counted as contributing to this strain.
                    const origin *win = m_provenance.of(kp.str());
                    if(win != nullptr && win->rank >= doc_band_min)
                        intro_ranks.insert(win->rank);
                }

                // No document-band entries for this strain: flat-source only,
                // skip the inheritance chain checks.
                if(intro_ranks.empty())
                    continue;

                const bool has_cross_layer = (intro_ranks.size() > 1);
                auto disp_it = disp_index.find({container.str(), key_value});
                const bool has_disposition = (disp_it != disp_index.end());

                if(has_cross_layer && !has_disposition)
                    return fail(nucleus::format(
                        "primary-key value '{}' in container '{}' is introduced "
                        "at multiple layers without an extend disposition: "
                        "re-opening a named instance in a derived file requires "
                        "an explicit extend attribute",
                        key_value, container.str()));

                if(has_disposition && !has_cross_layer)
                    return fail(nucleus::format(
                        "extend disposition for '{}' in container '{}' has no "
                        "base: no layer below the extending layer provides "
                        "entries for this instance",
                        key_value, container.str()));
            }

            // Unique-value enforcement across sibling instances.
            // For every non-identity unique field in this container, collect the
            // field value across all named strains and fail if any value appears
            // more than once. Runs before pruning so all strains are visible.
            for(const schema_element &uel : m_schema.elements())
            {
                if(!uel.unique || uel.identity)
                    continue;
                if(uel.container() != container)
                    continue;

                // For each strain bucket, look up the unique field's keyed path
                // and collect its value.
                std::map<std::string, std::vector<std::string>> value_to_strains;
                for(const auto &[kv, keyed_paths] : strains)
                {
                    // The unique field's path under this instance:
                    // container / kv / unique_field_name
                    const std::string unique_path =
                        container.str() + key_path::separator
                        + kv + key_path::separator + uel.name;
                    auto unique_kp = key_path::parse(unique_path);
                    if(!unique_kp.has_value())
                        continue;
                    const value *v = m_building.find(unique_kp.value());
                    if(v)
                        value_to_strains[std::string(v->text())].push_back(kv);
                }

                for(const auto &[val_text, kv_list] : value_to_strains)
                {
                    if(kv_list.size() > 1)
                    {
                        std::string which;
                        for(const std::string &strain_kv : kv_list)
                        {
                            if(!which.empty())
                                which += ", ";
                            which += nucleus::format("'{}'", strain_kv);
                        }
                        return fail(nucleus::format(
                            "unique field '{}' in container '{}' has duplicate "
                            "value '{}' across instances: {}",
                            uel.name, container.str(), val_text, which));
                    }
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

            // Determine if the chosen strain has a wide-extend disposition.
            bool wide_extend = false;
            {
                auto it = disp_index.find({container.str(), chosen});
                if(it != disp_index.end() && it->second == extend_strength::wide)
                    wide_extend = true;
            }

            // file_level general pre-pass: prune ALL paths whose winning rank
            // exceeds Ld. This sweeps keyed and general entries alike, and runs
            // on a snapshot to avoid iterator invalidation during removal.
            // Guard: if the chosen strain is extend-wide, its entries must survive
            // the pre-pass to compose regardless of the scope policy.
            if(policy == strain_scope_policy::file_level)
            {
                const std::string chosen_prefix =
                    container.str() + key_path::separator + chosen + key_path::separator;
                const std::vector<key_path> snapshot = m_building.paths();
                for(const key_path &path : snapshot)
                {
                    const origin *orig = m_provenance.of(path.str());
                    std::size_t path_rank = orig != nullptr ? orig->rank : 0;
                    if(orig == nullptr)
                    {
                        // Check collection origins for repeated leaves.
                        const std::vector<origin> *col_orig =
                            m_provenance.collection_origins_of(path.str());
                        if(col_orig != nullptr && !col_orig->empty())
                            path_rank = col_orig->front().rank;
                    }
                    if(path_rank == 0 || path_rank <= Ld)
                        continue;
                    // Skip entries belonging to the chosen strain when it is
                    // extend-wide: they must survive to compose via relay_strain.
                    if(wide_extend && path.str().compare(0, chosen_prefix.size(),
                                                         chosen_prefix) == 0)
                        continue;
                    m_building.remove(path);
                    m_provenance.forget(path.str());
                }
            }

            relay_strain(strains.at(chosen), policy, Ld, Ls, wide_extend);

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

    // Runs the typed conversion pass: for every schema element that has a
    // converter, visits the corresponding path in the post-slice building
    // keyspace. Absent typed paths are silently skipped (absence is orthogonal
    // to required-ness, which validate() enforces). A conversion failure fails
    // the resolve with the path, the converter's reason, and the winning layer
    // label from provenance. Must run after validate() and before freeze().
    [[nodiscard]] result<std::monostate, resolve_fold_error> convert()
    {
        for(const schema_element &el : m_schema.elements())
        {
            if(!el.converter || !el.type_identity.has_value())
                continue;

            const std::string path_str = el.declared_path().str();
            const auto kp_opt = key_path::parse(path_str);
            if(!kp_opt)
                continue;
            const key_path &kp = kp_opt.value();

            if(el.repeated)
            {
                const std::vector<value> *col = m_building.find_collection(kp);
                if(col == nullptr)
                    continue;

                std::vector<std::any> typed_col;
                typed_col.reserve(col->size());
                for(std::size_t i = 0; i < col->size(); ++i)
                {
                    auto res = el.converter((*col)[i].text());
                    if(!res)
                    {
                        std::string layer_label = "unknown layer";
                        const std::vector<origin> *co =
                            m_provenance.collection_origins_of(path_str);
                        if(co != nullptr && i < co->size())
                            layer_label = (*co)[i].layer;
                        return fail(nucleus::format(
                            "conversion failed for '{}' element [{}]: {} (layer: {})",
                            path_str, i, res.error(), layer_label));
                    }
                    typed_col.push_back(std::move(res).value());
                }
                m_typed_collections.emplace(path_str, std::move(typed_col));
            }
            else
            {
                const value *v = m_building.find(kp);
                if(v == nullptr)
                    continue;

                auto res = el.converter(v->text());
                if(!res)
                {
                    std::string layer_label = "unknown layer";
                    const origin *orig = m_provenance.of(path_str);
                    if(orig != nullptr)
                        layer_label = orig->layer;
                    return fail(nucleus::format(
                        "conversion failed for '{}': {} (layer: {})",
                        path_str, res.error(), layer_label));
                }
                m_typed.emplace(path_str, std::move(res).value());
            }
        }
        return std::monostate{};
    }

    // Copies every building value OUT into an owned snapshot and pairs it with the
    // provenance recorded alongside it, producing the immutable, self-owning
    // configuration. After this returns the context (and every retained buffer)
    // may be dropped: the configuration holds no view into any of them. The
    // collection branch is checked FIRST: find() returns nullptr for repeated
    // paths, so only find_collection() can reach them.
    [[nodiscard]] configuration freeze() const
    {
        std::map<std::string, std::string> owned;
        std::map<std::string, std::vector<std::string>> collections;
        for(const key_path &path : m_building.paths())
        {
            if(const std::vector<value> *col = m_building.find_collection(path))
            {
                std::vector<std::string> out;
                out.reserve(col->size());
                for(const value &v : *col)
                    out.push_back(std::string(v.text()));
                collections.emplace(path.str(), std::move(out));
            }
            else if(const value *v = m_building.find(path))
            {
                owned.emplace(path.str(), std::string(v->text()));
            }
        }
        return configuration(std::move(owned), std::move(collections),
                             m_typed, m_typed_collections, m_provenance);
    }

private:
    // Re-lays one strain's keyed entries onto the unified (key-stripped) paths,
    // applying the policy's rank-bounded filter to each entry's WINNING rank:
    // file_level and space_open_container_closed freeze the strain's keyed
    // entries at the defining layer Ld; container_open_until_next_strain admits
    // them up to but excluding Ls. An entry already at the unified path with a
    // higher winning rank (a flat override such as argv) displaces the keyed
    // value -- it composes by plain rank precedence, outside the bounds.
    //
    // When wide_extend=true the rank filter is bypassed entirely: the chosen
    // strain was declared with extend-wide, which is explicit consent to compose
    // all its entries regardless of the active scope policy.
    //
    // Repeated (collection) leaves under the keyed container are relayed
    // atomically: find_collection() is checked before find() so collections are
    // never silently dropped.
    void relay_strain(const std::vector<key_path> &keyed_paths,
                      strain_scope_policy policy, std::size_t Ld, std::size_t Ls,
                      bool wide_extend = false)
    {
        for(const key_path &keyed : keyed_paths)
        {
            const origin *from = m_provenance.of(keyed.str());
            std::size_t entry_rank = from != nullptr ? from->rank : 0;
            if(from == nullptr)
            {
                // Repeated leaf: scalar provenance is absent; derive rank from the
                // collection's winning layer instead.
                const std::vector<origin> *col_orig =
                    m_provenance.collection_origins_of(keyed.str());
                if(col_orig != nullptr && !col_orig->empty())
                    entry_rank = col_orig->front().rank;
                // If col_orig is null or empty, entry_rank stays 0, treating the
                // entry as introduced at the base layer. The exclusion filter will
                // not fire (0 <= Ld always) and the relay branch below will find no
                // data to relay -- a no-op, which is the safe default.
            }
            const bool excluded = !wide_extend && (
                policy == strain_scope_policy::container_open_until_next_strain
                    ? entry_rank >= Ls
                    : entry_rank > Ld);
            if(excluded)
            {
                m_building.remove(keyed);
                m_provenance.forget(keyed.str());
                continue;
            }

            auto unified = key_path::parse(m_schema.canonical_text(keyed));
            if(!unified)
                continue;

            if(!m_building.find(unified.value()))
            {
                if(const std::vector<value> *col = m_building.find_collection(keyed))
                {
                    // Repeated leaf: relay to the unified path. If a collection is
                    // already there (from a flat source fold), check rank so a
                    // higher-rank flat source's collection is not silently discarded.
                    const std::vector<origin> *existing_co =
                        m_provenance.collection_origins_of(unified.value().str());
                    const std::size_t existing_rank =
                        (existing_co != nullptr && !existing_co->empty())
                            ? existing_co->front().rank : 0;
                    if(existing_rank > entry_rank)
                    {
                        // Unified path already has a higher-rank collection; keyed
                        // collection is displaced -- drop it, keep the existing.
                    }
                    else
                    {
                        m_building.replace_collection(unified.value(),
                                                      std::vector<value>(*col));
                        if(const std::vector<origin> *co =
                               m_provenance.collection_origins_of(keyed.str()))
                            m_provenance.record_collection(unified.value().str(), *co);
                    }
                }
                else if(const value *v = m_building.find(keyed))
                {
                    // Scalar relay: unchanged.
                    m_building.set(unified.value(), *v);
                    if(from != nullptr)
                        m_provenance.record(unified.value().str(), *from);
                }
            }
            else
            {
                // A scalar already occupies the unified path; check displacement.
                const origin *at = m_provenance.of(unified.value().str());

                // For a collection leaf, scalar provenance (from) is absent; use
                // the collection's winning rank for the displacement comparison so
                // a lower-rank flat scalar cannot silence a higher-rank collection.
                const std::size_t effective_rank =
                    from != nullptr ? from->rank : entry_rank;
                const bool displaced =
                    at != nullptr && at->rank > effective_rank;

                if(!displaced)
                {
                    if(const std::vector<value> *col =
                           m_building.find_collection(keyed))
                    {
                        // Collection wins: replace the scalar at the unified path.
                        m_building.replace_collection(unified.value(),
                                                      std::vector<value>(*col));
                        if(const std::vector<origin> *co =
                               m_provenance.collection_origins_of(keyed.str()))
                            m_provenance.record_collection(
                                unified.value().str(), *co);
                    }
                    else if(const value *v = m_building.find(keyed))
                    {
                        m_building.set(unified.value(), *v);
                        if(from != nullptr)
                            m_provenance.record(unified.value().str(), *from);
                    }
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
    // Typed parallel maps populated by convert() -- scalar and repeated paths.
    std::map<std::string, std::any>              m_typed;
    std::map<std::string, std::vector<std::any>> m_typed_collections;
    std::vector<retained_buffer> m_buffers;
    // Containers whose single primary-keyed instance was sliced onto the unified
    // hierarchy -- evidence for the enforcer that their identity is satisfied
    // even though the key field was consumed and never appears as a leaf.
    std::vector<std::string> m_keyed_satisfied;
    // Re-open dispositions collected from all source batches during fold().
    // Consumed by slice() to enforce cross-layer re-open rules and drive
    // the relay_strain wide_extend bypass.
    std::vector<extend_disposition> m_dispositions;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RESOLUTION_CONTEXT_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RESOLUTION_CONTEXT_H

#include "nucleus/resolve/fold_entry.h"
#include "nucleus/resolve/cli_ordinal.h"
#include "nucleus/resolve/layer_fold.h"
#include "nucleus/resolve/keyed_divert.h"
#include "nucleus/resolve/resolve_types.h"
#include "nucleus/resolve/strain_relay.h"
#include "nucleus/resolve/repeated_sweep.h"
#include "nucleus/resolve/keyed_merge_state.h"
#include "nucleus/resolve/relayed_compaction.h"
#include "nucleus/resolve/keyed_collection_merge.h"

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/strain_scope.h"
#include "nucleus/config.h"

#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"
#include "nucleus/keyspace/storage_shape.h"

#include "nucleus/schema/instance_paths.h"
#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/group_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"

#include "nucleus/config_source/inherit_declaration.h"
#include "nucleus/config_source/source_handle.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/substitution_budget.h"
#include "nucleus/tokenizer/tree_resolver_scope.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <any>
#include <map>
#include <set>
#include <span>
#include <limits>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <typeindex>
#include <unordered_map>

namespace nucleus {

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
// call; freeze() copies values out into a self-owning config and the
// context (and with it every buffer) is destroyed.
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
        , m_divert(m_keyed, m_schema)
        , m_merge(m_building, m_provenance, m_keyed)
        , m_sweep(m_building, m_provenance, m_schema)
        , m_cli_ordinal(m_building, m_provenance, m_schema)
        , m_layers(m_schema, m_buffers, m_dispositions)
        , m_entry(m_layers, m_sweep, m_cli_ordinal, m_divert, m_building,
                  m_provenance, m_schema, m_tokenizer, m_tree_tokenizer)
        , m_compaction(m_building, m_provenance, m_schema, m_keyed)
        , m_relay(m_building, m_provenance, m_sweep, m_compaction, m_schema, m_keyed)
    {
    }

    // Borrowed by CONST reference and read-only, so concurrent loads on one shared
    // const config_space share nothing mutable and need no synchronization.
    const schema_registry &schema() const noexcept { return m_schema; }
    const tokenizer_registry &tokenizer() const noexcept { return m_tokenizer; }
    // Borrowed (never owned), like the other siblings; read by convert() to supply
    // a converter for a typed element that carries no per-element converter.
    const converter_registry &converters() const noexcept { return m_converters; }

    keyspace &building() noexcept { return m_building; }
    provenance &origins() noexcept { return m_provenance; }

    // Fold overload that consumes a sequence of layered_handle descriptors.
    // The caller assigns ascending ranks for cross-source precedence; low rank
    // folds first and equal ranks keep their input order. Each handle is pulled
    // exactly once per load; the project->pull->inherit contract holds unchanged.
    //
    // Unified storage: ALL repeated paths (leaves and containers) are stored as
    // indexed scalars in m_building. "config/tag" with duplicate entries becomes
    // "config/tag[0]", "config/tag[1]" etc. "cluster/node[0]/port" from a
    // document source is stored directly. No collection maps used.
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

    // Pass between fold() and slice(): applies the per-collection merge mode to the
    // diverted keyed collections. Runs before slice() so the merge sees every layer
    // while the key is still present; slice() later strips the transient
    // strain-key segments.
    expected<void, resolve_fold_error> merge_keyed_collections()
    {
        return m_merge.merge();
    }

    // Collapses keyed-container instances into the ONE unified hierarchy the
    // resolved config promises. A primary-key value is internal to
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
    expected<void, resolve_fold_error>
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
                return unexpected(error{errc::invalid_selection, nucleus::format(
                    "selection '{}' cannot be applied: the schema declares "
                    "no primary key",
                    selection.value())});
        }

        for(const schema_element &el : m_schema.elements())
        {
            if(!el.identity)
                continue;

            const key_path    container     = el.container();
            const std::string identity_path = el.declared_path().str();

            // Bucket every keyed leaf by its instance's key value. paths() is a
            // snapshot, so mutating the keyspace below is safe. A key value that
            // shadows a declared element name can never be bucketed -- report it
            // loudly here instead of letting validation fail later with an
            // unrelated unknown-key suggestion.
            std::map<std::string, std::vector<key_path>> strains;
            for(const key_path &path : m_building.paths())
            {
                // Skip paths where the segment after the container is an ordinal
                // index -- those are flat-source repeated leaves, not keyed instances.
                // An indexed-shaped segment is only a legitimate ordinal when a
                // repeated element is genuinely declared directly at this container
                // under that base name; otherwise it is a strain whose primary-key
                // value merely happens to be bracket-shaped, and must not vanish here.
                if(path.size() > container.size()
                   && key_path::is_indexed_segment(path.segments()[container.size()]))
                {
                    const std::string base = std::string(
                        key_path::base_name(path.segments()[container.size()]));
                    const key_path declared_child = container.child(base);
                    bool declared_repeated_child = false;
                    for(const schema_element &child_el : m_schema.elements())
                    {
                        if(child_el.repeated && child_el.declared_path() == declared_child)
                        {
                            declared_repeated_child = true;
                            break;
                        }
                    }
                    if(declared_repeated_child)
                        continue;
                    return unexpected(error{errc::schema_violation, nucleus::format(
                        "primary-key value '{}' in container '{}' is shaped like a "
                        "repeated-instance ordinal ('[n]'), which this container does "
                        "not declare a repeated child at; rename the key value",
                        path.segments()[container.size()], container.str())});
                }

                if(m_schema.keyed_instance_path(container, path))
                    strains[path.segments()[container.size()]].push_back(path);
                else if(m_schema.key_value_collision(container, path))
                    return unexpected(error{errc::schema_violation, nucleus::format(
                        "primary-key value '{}' in container '{}' collides with "
                        "a declared element of the same name: a strain cannot "
                        "be keyed by a sibling element's name",
                        path.segments()[container.size()], container.str())});
            }

            if(strains.empty())
            {
                // A selection against a container holding no keyed instances is
                // unsatisfiable and must fail loudly, never silently resolve to
                // whatever template content exists.
                if(selection.has_value())
                    return unexpected(error{errc::invalid_selection, nucleus::format(
                        "selection '{}' does not match any strain in container "
                        "'{}': the container holds no primary-keyed instances",
                        selection.value(), container.str())});
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
                if(!strains.contains(selection.value()))
                {
                    std::string available;
                    for(const auto &[key_value, _] : strains)
                    {
                        if(!available.empty())
                            available += ", ";
                        available += nucleus::format("'{}'", key_value);
                    }
                    return unexpected(error{errc::invalid_selection, nucleus::format(
                        "selection '{}' does not match any strain in container "
                        "'{}'; available: {}",
                        selection.value(), container.str(), available)});
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
                return unexpected(error{errc::invalid_selection, nucleus::format(
                    "container '{}' holds {} primary-keyed instances ({}) and "
                    "no instance is selected: a config space resolves "
                    "exactly one",
                    container.str(), strains.size(), keys)});
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
                    return unexpected(error{errc::layering_violation, nucleus::format(
                        "strain '{}' in container '{}' has no recorded "
                        "provenance: resolve cannot bound its defining layer",
                        chosen, container.str())});
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
            // These checks apply only to inheritance-chain entries, identified by
            // the explicit inheritance-layer channel on each origin. Flat source
            // layering (env, argv, defaults) carries no inheritance layer, forms a
            // single flat layer by design, and is never a re-open error.
            // For each named strain: compute the set of distinct inheritance-chain
            // layer ordinals at which entries were laid down, combining
            // first-introduction layers with winning layers. An entry overwritten by
            // a higher chain layer has its first-introduction layer (the base) AND
            // its winning layer (the deriving file) both recorded -- so a chain
            // re-open via overwrite is detected as multi-layer even when the
            // overwrite collapses the building keyspace to a single path. A strain
            // present at more than one inheritance layer without an extend
            // disposition is a re-open error. A strain with an extend disposition but
            // entries at only one inheritance layer has no base (extend without base).
            for(const auto &[key_value, keyed_paths] : strains)
            {
                std::set<std::size_t> intro_layers;
                for(const key_path &kp : keyed_paths)
                {
                    const std::size_t *first =
                        m_provenance.first_inheritance_layer_of(kp.str());
                    if(first != nullptr)
                        intro_layers.insert(*first);
                    // Also include the winning layer: an overwrite by a higher chain
                    // layer means the winner and the first-introducer differ, and both
                    // inheritance layers must be counted as contributing to this strain.
                    const origin *win = m_provenance.of(kp.str());
                    if(win != nullptr && win->inheritance_layer.has_value())
                        intro_layers.insert(win->inheritance_layer.value());
                }

                // No inheritance-chain entries for this strain: flat-source only,
                // skip the inheritance chain checks.
                if(intro_layers.empty())
                    continue;

                const bool has_cross_layer = (intro_layers.size() > 1);
                auto disp_it = disp_index.find({container.str(), key_value});
                const bool has_disposition = (disp_it != disp_index.end());

                if(has_cross_layer && !has_disposition)
                    return unexpected(error{errc::layering_violation,
                        nucleus::format(
                            "primary-key value '{}' in container '{}' is introduced "
                            "at multiple layers without an extend disposition: "
                            "re-opening a named instance in a derived file requires "
                            "an explicit extend attribute",
                            key_value, container.str())});

                if(has_disposition && !has_cross_layer)
                    return unexpected(error{errc::layering_violation,
                        nucleus::format(
                            "extend disposition for '{}' in container '{}' has no "
                            "base: no layer below the extending layer provides "
                            "entries for this instance",
                            key_value, container.str())});
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
                        return unexpected(error{errc::layering_violation,
                            nucleus::format(
                                "unique field '{}' in container '{}' has duplicate "
                                "value '{}' across instances: {}",
                                uel.name, container.str(), val_text, which)});
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
                    // Flat unified-path writes (argv/env) carry no inheritance_layer
                    // and always win by plain stack precedence, per strain_scope.h's
                    // documented contract; exempt them from the rank-bounded prune.
                    if(orig != nullptr && !orig->inheritance_layer.has_value())
                        continue;
                    const std::size_t path_rank = orig != nullptr ? orig->rank : 0;
                    if(path_rank == 0 || path_rank <= Ld)
                        continue;
                    // Keyed-merge collections were finalised across layers already;
                    // never rank-prune them here either.
                    if(m_keyed.under_keyed_merge(m_schema.canonical_text(path)))
                        continue;
                    // The chosen identity always survives; extend-wide preserves
                    // every other chosen entry for relay_strain as well.
                    if(path.str().starts_with(chosen_prefix) && (wide_extend || m_schema.canonical_text(path) == identity_path))
                        continue;
                    m_building.remove(path);
                    m_provenance.forget(path.str());
                }
            }

            if(auto r = m_relay.relay_strain(strains.at(chosen), identity_path,
                                             policy, Ld, Ls, wide_extend);
               !r)
                return r;

            // The strain's key value named the instance and was consumed; the
            // enforcer's identity-presence check is satisfied structurally.
            m_keyed_satisfied.push_back(container.str());
        }

        return {};
    }

    expected<void, resolve_fold_error> apply_deferred_cli_overrides()
    {
        return m_cli_ordinal.apply();
    }

    // Pass-2 tree-reference resolution: runs after slice() and before validate().
    // Resolves all ${abs:} and ${rel:} tokens in the sliced keyspace, writing back
    // the resolved strings to the same paths. Enforces:
    //   - Value-only invariant: a key segment containing "${" is a loud error.
    //   - Cross-leaf cycle detection via expansion_guard.
    //   - Substitution-count budget: budget_exceeded stops billion-laughs.
    //   - ?? chaining: missing_field falls through; all other errors propagate.
    expected<void, resolve_fold_error> resolve_references()
    {
        // Value-only invariant scan: path segments must not contain "${".
        // The tree shape is frozen by slice(); references may only appear in values.
        for(const key_path &kp : m_building.paths())
        {
            for(const std::string &seg : kp.segments())
            {
                if(seg.find("${") != std::string::npos)
                    return unexpected(error{errc::unresolved_token,
                        nucleus::format("reference in structural key position: '{}'",
                                       kp.str())});
            }
        }

        expansion_guard leaf_guard(default_reference_depth_cap);
        std::unordered_map<std::string, std::string> resolved_cache;
        substitution_budget budget(m_reference_budget);

        // Snapshot paths (resolving writes back via m_building.set()).
        std::vector<key_path> const all_paths = m_building.paths();

        for(const key_path &kp : all_paths)
        {
            const value *v = m_building.find(kp);
            if(v == nullptr)
                continue;
            const std::string_view text = v->text();
            if(text.find("${") == std::string_view::npos)
                continue;

            auto r = resolve_one_leaf(kp, leaf_guard, resolved_cache, budget);
            if(!r)
            {
                const resolve_error &re = r.error();
                return unexpected(error{errc::unresolved_token,
                    nucleus::format("reference resolution failed for '{}': {}",
                                   kp.str(), re.message)});
            }
        }

        return {};
    }

    // Validates the folded keyspace against the borrowed schema -- the step that
    // makes the schema authoritative over CONTENT at resolve time, reached only
    // through ctx.schema() so the registry stays a borrowed sibling. Runs ONLY
    // when the schema declares a surface: a host that registers no schema gets no
    // content gate (an empty schema is not a claim that nothing is allowed). An
    // undeclared key is reported with its nearest declared neighbor so a typo is
    // actionable; missing required fields are reported by the enforcer.
    expected<void, resolve_fold_error> validate()
    {
        const std::vector<key_path> paths = m_building.paths();
        if(auto shape = validate_storage_shape(paths); !shape)
            return unexpected(std::move(shape).error());

        const bool has_groups = !m_schema.constraint_groups().empty()
                             || !m_schema.identity_groups().empty();

        // The unknown-path/required checks only apply when the schema declares a
        // surface (an empty schema is not a claim that nothing is allowed), so they
        // short-circuit here. The group/identity pass is orthogonal: it runs whenever
        // any group is registered, independent of the surface -- a root-anchored
        // host-validator valve carries no element surface yet must still enforce.
        if(m_schema.surface().empty() && !has_groups)
            return {};

        schema_validation checked = m_schema.surface().empty()
            ? schema_validation{}
            : schema_enforcer::validate(m_schema, m_building, m_keyed_satisfied);

        // Container-scoped constraint + identity groups enforce over the resolved,
        // sliced tree -- run on a transient config snapshot so the host-validator valve and
        // member navigation use the real config_node walk. Skipped when no group is
        // declared (the common case pays nothing).
        std::vector<schema_violation> group_violations;
        if(has_groups)
        {
            std::map<std::string, std::string> owned;
            for(const key_path &path : m_building.paths())
                if(const value *v = m_building.find(path))
                    owned.emplace(path.str(), std::string(v->text()));
            config const snapshot(std::move(owned), m_provenance);
            group_violations = group_enforcer::validate(m_schema, snapshot);
        }

        if(checked && group_violations.empty())
            return {};

        const std::vector<key_path> surface = m_schema.surface();
        std::vector<std::string> known;
        known.reserve(surface.size());
        for(const key_path &path : surface)
            known.push_back(path.str());

        std::string report = "schema validation failed:";
        // Unknown-path / required violations get a did-you-mean; group violations
        // already name their parties precisely, so they carry no spurious suggestion.
        if(!checked)
        {
            for(const schema_violation &v : checked.error())
            {
                report += nucleus::format("\n  - {}", v.reason);
                if(!m_schema.recognizes_text(m_sweep.canonical_of(v.path)))
                {
                    auto near = suggest_keys(v.path, known, 1);
                    if(!near.empty())
                        report += nucleus::format(" (did you mean '{}'?)", near.front());
                }
            }
        }
        for(const schema_violation &v : group_violations)
            report += nucleus::format("\n  - {}", v.reason);
        return unexpected(error{errc::schema_violation, std::move(report)});
    }

    // Runs the typed conversion pass: for every typed schema element, resolves the
    // effective converter and converts corresponding paths in the building keyspace.
    // Repeated elements store per-instance typed values keyed by indexed path
    // (e.g. "cluster/node[0]/port"). Must run after validate() and before freeze().
    expected<void, resolve_fold_error> convert()
    {
        for(const schema_element &el : m_schema.elements())
        {
            if(!el.type_identity.has_value())
                continue;

            const converter_registry::converter *conv =
                el.converter ? &el.converter
                             : m_converters.find(el.type_identity.value());
            if(conv == nullptr)
                continue;

            const std::string path_str = el.declared_path().str();

            // For both repeated elements and non-repeated elements that live under a
            // repeated container, iterate all indexed paths whose canonical form
            // matches the declared path and convert each instance independently.
            // A non-repeated leaf under a repeated container (e.g. cluster/node/port
            // when node is repeated) has no scalar at the plain declared path; its
            // values live at cluster/node[0]/port, cluster/node[1]/port, etc.
            bool found_any_indexed = false;
            for(const key_path &kp : m_building.paths())
            {
                if(m_schema.canonical_text(kp) != path_str)
                    continue;
                if(kp.str() == path_str)
                    continue;
                found_any_indexed = true;
                const value *v = m_building.find(kp);
                if(v == nullptr)
                    continue;
                auto res = (*conv)(v->text());
                if(!res)
                {
                    std::string layer_label = "unknown layer";
                    const origin *orig = m_provenance.of(kp.str());
                    if(orig != nullptr)
                        layer_label = orig->layer;
                    return unexpected(error{errc::failed_conversion,
                        nucleus::format(
                            "conversion failed for '{}': {} (layer: {})",
                            kp.str(), res.error(), layer_label)});
                }
                m_typed.emplace(kp.str(), std::move(res).value());
            }

            // Direct path lookup: plain (non-indexed) scalar at the declared path.
            if(!found_any_indexed)
            {
                const auto kp_opt = key_path::parse(path_str);
                if(!kp_opt)
                    continue;
                const value *v = m_building.find(kp_opt.value());
                if(v == nullptr)
                    continue;
                auto res = (*conv)(v->text());
                if(!res)
                {
                    std::string layer_label = "unknown layer";
                    const origin *orig = m_provenance.of(path_str);
                    if(orig != nullptr)
                        layer_label = orig->layer;
                    return unexpected(error{errc::failed_conversion, nucleus::format(
                        "conversion failed for '{}': {} (layer: {})",
                        path_str, res.error(), layer_label)});
                }
                m_typed.emplace(path_str, std::move(res).value());
            }
        }
        return {};
    }

    // Copies every building value OUT into an owned snapshot and pairs it with the
    // provenance recorded alongside it, producing the immutable, self-owning config.
    // All repeated paths are indexed scalars in m_building; no collection branch needed.
    config freeze(std::vector<degradation> degraded = {}) const
    {
        std::map<std::string, std::string> owned;
        for(const key_path &path : m_building.paths())
        {
            if(const value *v = m_building.find(path))
                owned.emplace(path.str(), std::string(v->text()));
        }
        return {std::move(owned), m_typed, m_provenance, std::move(degraded)};
    }

    // Sets the pass-2 reference substitution budget. 0 maps to the engine default (never zero-cap).
    void set_reference_budget(std::size_t budget) noexcept
    {
        if(budget != 0)
            m_reference_budget = budget;
    }

    // Sets the pass-1 expansion substitution budget. 0 maps to the engine default (never zero-cap).
    // Must be called before fold(), which is where pass-1 expansion runs.
    void set_expansion_budget(std::size_t budget) noexcept
    {
        if(budget != 0)
            m_expansion_budget = budget;
    }

private:
    // Recursive single-leaf resolver for pass-2. Resolves `kp`'s value by first
    // ensuring all leaves it references are themselves resolved (depth-first).
    // The expansion_guard detects cycles; the cache prevents repeated work.
    // Returns a resolve_error (not resolve_fold_error) so it integrates with the
    // ensure_resolved_fn callback type used by tree_resolver_scope.
    expected<void, resolve_error>
    resolve_one_leaf(const key_path &kp,
                     expansion_guard &leaf_guard,
                     std::unordered_map<std::string, std::string> &resolved_cache,
                     substitution_budget &budget)
    {
        const std::string path_str = kp.str();

        // Already resolved in this pass.
        if(resolved_cache.contains(path_str))
            return {};

        const value *v = m_building.find(kp);
        if(v == nullptr)
            return {};

        const std::string_view text = v->text();

        // Nothing to resolve for this leaf (no tree-access tokens).
        // After pass-1 fold, any remaining ${ is a tree-access token (abs:, rel:,
        // or a registered tree-tokenizer category) that pass-1 left verbatim.
        if(text.find("${") == std::string_view::npos)
        {
            resolved_cache[path_str] = std::string(text);
            return {};
        }

        // Enter cross-leaf guard — detects A -> B -> A cycles.
        auto guard_scope = leaf_guard.enter(path_str);
        if(!guard_scope)
            return unexpected(std::move(guard_scope).error());

        // Build the ensure_resolved callback for depth-first recursive resolution.
        // Captures this, leaf_guard, resolved_cache, and budget by ref.
        ensure_resolved_fn ensure = [&](const key_path &target)
            -> expected<void, resolve_error>
        {
            return resolve_one_leaf(target, leaf_guard, resolved_cache, budget);
        };

        tree_resolver_scope scope(m_building, kp, budget,
                                  std::move(ensure), &m_tree_tokenizer);
        auto resolved = scope.resolve_value(text);
        if(!resolved)
            return unexpected(std::move(resolved).error());

        resolved_cache[path_str] = resolved.value();
        m_building.set(kp, value::owned(std::move(resolved).value()));
        return {};
    }

    const schema_registry           &m_schema;
    const tokenizer_registry        &m_tokenizer;
    const converter_registry        &m_converters;
    const tree_tokenizer_registry   &m_tree_tokenizer;

    std::size_t m_reference_budget = default_reference_budget;
    std::size_t m_expansion_budget = default_expansion_budget;

    keyspace m_building;
    provenance m_provenance;
    // Typed values populated by convert(). All repeated-path instances are keyed
    // by their full indexed path string (e.g. "cluster/node[0]/port").
    std::map<std::string, std::any> m_typed;
    std::vector<retained_buffer> m_buffers;
    // Containers whose single primary-keyed instance was sliced onto the unified
    // hierarchy -- evidence for the enforcer that their identity is satisfied
    // even though the key field was consumed and never appears as a leaf.
    std::vector<std::string> m_keyed_satisfied;
    // Re-open dispositions collected from all source batches during fold().
    // Consumed by slice() to enforce cross-layer re-open rules and drive
    // the relay_strain wide_extend bypass.
    std::vector<extend_disposition> m_dispositions;

    keyed_merge_state m_keyed;
    keyed_divert m_divert;
    keyed_collection_merge m_merge;
    repeated_sweep m_sweep;
    cli_ordinal m_cli_ordinal;
    layer_fold m_layers;
    fold_entry m_entry;
    relayed_compaction m_compaction;
    strain_relay m_relay;
};

}

#endif

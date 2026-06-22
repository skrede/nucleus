#ifndef HPP_GUARD_NUCLEUS_RESOLVE_RESOLUTION_CONTEXT_H
#define HPP_GUARD_NUCLEUS_RESOLVE_RESOLUTION_CONTEXT_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/strain_scope.h"
#include "nucleus/config.h"

#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"

#include "nucleus/config_source/inherit_declaration.h"
#include "nucleus/config_source/source_handle.h"

#include "nucleus/diagnostics/key_suggester.h"

#include "nucleus/tokenizer/token_resolution.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

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
#include <typeindex>

namespace nucleus {

// The error a resolve fold can report: a source pull failure or a token
// resolution failure, surfaced verbatim with the offending layer named.
using resolve_fold_error = error;

// Maximum total reference substitutions across one pass-2 resolve. Stops billion-laughs amplification.
inline constexpr std::size_t default_reference_budget = 10000;
// Maximum cross-leaf reference chain depth. Per-value nesting cap (16) stays in expansion_guard.h.
inline constexpr std::size_t default_reference_depth_cap = 64;

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
    resolution_context(const schema_registry &schema,
                        const tokenizer_registry &tokenizer,
                        const converter_registry &converters) noexcept
        : m_schema(schema), m_tokenizer(tokenizer), m_converters(converters)
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

    // One entry in the handle-based fold: the erased source, its ascending rank
    // (cross-source precedence), a human-readable label for provenance, and an
    // optional inheritance-chain layer ordinal. The layer is present only for
    // inheritance-chain entries (base lowest); flat sources leave it absent and
    // are treated as a single flat layer exempt from the slice re-open rules.
    struct layered_handle
    {
        source_handle *handle;
        std::size_t    rank;
        std::string    label;
        owner_token    owner;
        std::optional<std::size_t> inheritance_layer;
    };

    // Fold overload that consumes a sequence of layered_handle descriptors.
    // The caller assigns ascending ranks for cross-source precedence; the
    // stable_sort folds low rank first. Each handle is pulled exactly once per
    // load; the project->pull->inherit lifecycle contract holds unchanged.
    //
    // Unified storage: ALL repeated paths (leaves and containers) are stored as
    // indexed scalars in m_building. "config/tag" with duplicate entries becomes
    // "config/tag[0]", "config/tag[1]" etc. "cluster/node[0]/port" from a
    // document source is stored directly. No collection maps used.
    expected<void, resolve_fold_error>
    fold(std::span<layered_handle> layers)
    {
        std::vector<layered_handle *> ordered;
        ordered.reserve(layers.size());
        for(layered_handle &lh : layers)
            ordered.push_back(&lh);
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const layered_handle *a, const layered_handle *b) {
                             return a->rank < b->rank;
                         });

        const schema_projection projection = m_schema.projection();

        std::set<std::string> repeated_paths;
        for(const schema_element &el : m_schema.elements())
        {
            if(el.repeated)
                repeated_paths.insert(el.declared_path().str());
        }

        // Repeated container prefixes: declared paths of repeated elements that
        // have child elements. Used for wholesale-replace and extend= guard.
        const std::set<std::string> repeated_container_prefixes =
            m_schema.repeated_container_paths();

        // Deferred D-11 checks: CLI plain-ordinal overrides deferred until after
        // all layers fold so the document layer is present in m_building regardless
        // of its rank relative to the argv layer.
        struct pending_cli_ordinal
        {
            std::size_t     ordinal;
            std::string     container_prefix;
            key_path        rebracketed;
            value           val;
            origin          prov;
        };
        std::vector<pending_cli_ordinal> deferred_cli_overrides;

        for(layered_handle *lh : ordered)
        {
            lh->handle->apply_projection(projection);
            config_source_result pulled = lh->handle->pull();
            if(!pulled)
                return unexpected(error{pulled.error().code,
                                        nucleus::format("source '{}': {}",
                                            lh->label, pulled.error().message)});

            config_source_batch &batch = pulled.value();

            // D-19: extend= targeting a repeated container is not supported.
            for(const extend_disposition &d : batch.dispositions)
            {
                if(repeated_container_prefixes.count(d.container_path))
                    return unexpected(error{errc::layering_violation,
                        nucleus::format(
                            "extend= targeting repeated container '{}' is not "
                            "supported: repeated containers replace wholesale "
                            "across layers",
                            d.container_path)});
            }

            // Per-layer counters for repeated leaves arriving as plain paths
            // (duplicate_keys sources like runtime_source or tree sources with flat
            // repeated leaves). Keyed by canonical path.
            std::map<std::string, std::size_t> leaf_ordinal_counters;

            // Tracks which container prefixes have had their wholesale-replace
            // sweep done in this layer.
            std::set<std::string> swept_containers_this_layer;

            for(keyspace_entry &entry : batch.entries)
            {
                token_result expanded = resolve_tokens(entry.value.text(), m_tokenizer);
                if(!expanded)
                    return unexpected(error{errc::unresolved_token, nucleus::format(
                        "source '{}': token resolution failed for key '{}': {}",
                        lh->label, entry.path, expanded.error().message)});

                auto path_res = key_path::parse(entry.path);
                if(!path_res)
                    continue;
                key_path path = std::move(path_res).value();

                const std::string canonical_path = m_schema.canonical_text(path);

                // Determine if this entry is an already-indexed path from a
                // document source (e.g. "cluster/node[0]/port" from a tree source).
                const bool is_already_indexed = [&]() {
                    for(const std::string &seg : path.segments())
                        if(key_path::is_indexed_segment(seg))
                            return true;
                    return false;
                }();

                // Detect a CLI plain-ordinal path (D-09): a digit-only segment
                // following a repeated container prefix is an ordinal index from
                // "--cluster-node-0-endpoint-port=90" -> "cluster/node/0/endpoint/port".
                // Re-bracket to "cluster/node[0]/endpoint/port" and enforce D-11
                // (override-only: ordinal must be < existing instance count).
                const auto plain_ordinal_rebracketed = [&]()
                    -> expected<std::optional<key_path>, resolve_fold_error>
                {
                    const std::vector<std::string> &segs = path.segments();
                    for(std::size_t i = 1; i < segs.size(); ++i)
                    {
                        // Plain digit-only segment (e.g. "0", "42") -- not bracket form.
                        const std::string &seg = segs[i];
                        const bool all_digits = std::ranges::all_of(
                            seg, [](char c){ return c >= '0' && c <= '9'; });
                        if(!all_digits)
                            continue;
                        // Build the prefix path up to (not including) the digit segment.
                        std::string prefix;
                        for(std::size_t j = 0; j < i; ++j)
                        {
                            if(j) prefix += key_path::separator;
                            prefix += segs[j];
                        }
                        if(!repeated_container_prefixes.count(prefix))
                            continue;
                        // Found: "prefix/N/..." is a CLI ordinal path.
                        const std::size_t ordinal = [&]() {
                            std::size_t v = 0;
                            for(char c : seg) v = v * 10 + static_cast<std::size_t>(c - '0');
                            return v;
                        }();
                        // Re-bracket: "prefix/N/rest" -> "prefix[N]/rest".
                        std::string rebracketed_str = prefix + "[" + std::to_string(ordinal) + "]";
                        for(std::size_t j = i + 1; j < segs.size(); ++j)
                        {
                            rebracketed_str += key_path::separator;
                            rebracketed_str += segs[j];
                        }
                        auto kp = key_path::parse(rebracketed_str);
                        if(!kp)
                            return unexpected(error{errc::malformed_source, std::move(kp).error()});
                        // Defer storage and D-11 check until all layers are folded
                        // so the document source is in m_building regardless of rank.
                        deferred_cli_overrides.push_back(
                            {ordinal, prefix, std::move(kp).value(),
                             value::owned(std::move(expanded).value()),
                             origin{lh->rank, lh->label, lh->owner, lh->inheritance_layer}});
                        // Signal "deferred": return a non-empty optional wrapping the
                        // zero-segment key_path as a sentinel (empty path cannot appear
                        // as a real indexed result, so the has_value() check below
                        // disambiguates via a separate cli_deferred_this_entry flag).
                        return std::optional<key_path>{key_path{}};
                    }
                    return std::optional<key_path>{std::nullopt};
                }();
                if(!plain_ordinal_rebracketed)
                    return unexpected(std::move(plain_ordinal_rebracketed).error());

                const bool cli_deferred_this_entry =
                    plain_ordinal_rebracketed.value().has_value()
                    && plain_ordinal_rebracketed.value().value().empty();

                if(cli_deferred_this_entry)
                    continue; // storage and D-11 check deferred to post-fold pass

                if(plain_ordinal_rebracketed.value().has_value())
                {
                    // Case C: CLI plain-ordinal override (D-09/D-11). The path has
                    // been re-bracketed; store directly without wholesale-replace so
                    // only this specific instance is overridden (rank-precedence wins).
                    const key_path &rebracketed_path = plain_ordinal_rebracketed.value().value();
                    m_building.set(rebracketed_path, value::owned(std::move(expanded).value()));
                    m_provenance.record(rebracketed_path.str(),
                                        origin{lh->rank, lh->label, lh->owner,
                                               lh->inheritance_layer});
                }
                else if(is_already_indexed)
                {
                    // Case B: already-indexed path (from a tree source's ordinal emission).
                    // Find the container prefix (the declared repeated container path).
                    std::string container_prefix;
                    for(const std::string &prefix : repeated_container_prefixes)
                    {
                        // The canonical of this path must start with prefix + separator.
                        const std::string p_slash = prefix + key_path::separator;
                        if(canonical_path == prefix
                           || canonical_path.compare(0, p_slash.size(), p_slash) == 0)
                        {
                            container_prefix = prefix;
                            break;
                        }
                    }
                    // Also handle repeated leaves with indexed paths (config/tags[0]).
                    if(container_prefix.empty() && repeated_paths.count(canonical_path))
                        container_prefix = canonical_path;

                    if(!container_prefix.empty()
                       && !swept_containers_this_layer.count(container_prefix))
                    {
                        // D-06 wholesale-replace: on first entry from a new layer
                        // touching this container, remove all existing entries.
                        swept_containers_this_layer.insert(container_prefix);
                        const std::string cp_slash =
                            container_prefix + key_path::separator;
                        const std::vector<key_path> snapshot = m_building.paths();
                        for(const key_path &existing : snapshot)
                        {
                            const std::string es = existing.str();
                            // Remove entries that belong to this container:
                            // either their canonical starts with the prefix or equals it.
                            const std::string ec = m_schema.canonical_text(existing);
                            const bool under =
                                ec == container_prefix
                                || ec.compare(0, cp_slash.size(), cp_slash) == 0;
                            if(under)
                            {
                                m_building.remove(existing);
                                m_provenance.forget(es);
                            }
                        }
                    }

                    m_building.set(path, value::owned(std::move(expanded).value()));
                    m_provenance.record(entry.path,
                                        origin{lh->rank, lh->label, lh->owner,
                                               lh->inheritance_layer});
                }
                else if(repeated_paths.count(canonical_path))
                {
                    // Case A: plain repeated-leaf entry arriving as a plain path
                    // (from duplicate_keys sources like runtime_source or tree sources).
                    // Track ordinals by ACTUAL path for per-strain independence;
                    // wholesale-replace by CANONICAL path so all prior-layer entries
                    // for this repeated field are evicted on first new-layer access.
                    const std::string &actual_path_str = entry.path;

                    if(!entry.capabilities.supports(capability::duplicate_keys)
                       && leaf_ordinal_counters.count(actual_path_str) != 0)
                    {
                        return unexpected(error{errc::layering_violation,
                            nucleus::format(
                                "source '{}': repeated field '{}' received multiple "
                                "values from a source that does not support "
                                "duplicate_keys; a flat source can supply at most one "
                                "value per repeated field per layer",
                                lh->label, entry.path)});
                    }

                    // Cross-layer wholesale-replace: on first touch of this canonical
                    // repeated path in this layer, sweep existing flat (non-keyed)
                    // entries whose canonical form matches. Keyed entries (paths that
                    // contain transient key-value segments) are intentionally left in
                    // the building keyspace so slice() can still find and relay the
                    // strain; relay_strain handles displacement via its rank check.
                    if(!swept_containers_this_layer.count(canonical_path))
                    {
                        swept_containers_this_layer.insert(canonical_path);
                        const std::string cp_bracket = canonical_path + "[";
                        const std::vector<key_path> snapshot = m_building.paths();
                        for(const key_path &existing : snapshot)
                        {
                            const std::string es = existing.str();
                            // Only sweep flat entries: those that ARE the canonical path
                            // or are directly-indexed versions of it (canonical_path[N]).
                            if(es != canonical_path
                               && es.compare(0, cp_bracket.size(), cp_bracket) != 0)
                                continue;
                            m_building.remove(existing);
                            m_provenance.forget(es);
                        }
                    }

                    const std::size_t ordinal = leaf_ordinal_counters[actual_path_str]++;
                    // Store with ordinal appended to the ACTUAL path (not canonical),
                    // preserving any key segment for relay_strain to find.
                    const std::string indexed_path =
                        actual_path_str + "[" + std::to_string(ordinal) + "]";
                    auto indexed_kp = key_path::parse(indexed_path);
                    if(!indexed_kp)
                        continue;
                    m_building.set(indexed_kp.value(),
                                   value::owned(std::move(expanded).value()));
                    m_provenance.record(indexed_path,
                                        origin{lh->rank, lh->label, lh->owner,
                                               lh->inheritance_layer});
                }
                else
                {
                    m_building.set(path, value::owned(std::move(expanded).value()));
                    m_provenance.record(entry.path,
                                        origin{lh->rank, lh->label, lh->owner,
                                               lh->inheritance_layer});
                }
            }

            for(const extend_disposition &d : batch.dispositions)
                m_dispositions.push_back(d);

            m_buffers.push_back(std::move(batch.buffer));
        }

        // D-11 post-fold: validate and store all deferred CLI plain-ordinal overrides
        // now that all document layers are present in m_building.
        for(pending_cli_ordinal &override : deferred_cli_overrides)
        {
            const std::string bracket_prefix = override.container_prefix + "[";
            std::size_t instance_count = 0;
            for(const key_path &bp : m_building.paths())
            {
                const std::string bps = bp.str();
                if(bps.compare(0, bracket_prefix.size(), bracket_prefix) != 0)
                    continue;
                const auto lb = bps.find('[', override.container_prefix.size());
                const auto rb = bps.find(']', lb);
                if(lb == std::string::npos || rb == std::string::npos)
                    continue;
                std::size_t slot = 0;
                for(std::size_t k = lb + 1; k < rb; ++k)
                    slot = slot * 10 + static_cast<std::size_t>(bps[k] - '0');
                if(slot + 1 > instance_count)
                    instance_count = slot + 1;
            }
            if(override.ordinal >= instance_count)
                return unexpected(error{errc::schema_violation, nucleus::format(
                    "argv ordinal {} for '{}' is out of range: "
                    "{} instance(s) exist; out of range",
                    override.ordinal, override.container_prefix, instance_count)});
            // Ordinal is valid; store only if no higher-rank entry already exists
            // at this path (rank-precedence: higher-rank wins even over deferred entries).
            const origin *existing = m_provenance.of(override.rebracketed.str());
            if(existing == nullptr || existing->rank < override.prov.rank)
            {
                m_building.set(override.rebracketed, override.val);
                m_provenance.record(override.rebracketed.str(), override.prov);
            }
        }

        return {};
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

            const key_path container = el.container();

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
                if(path.size() > container.size()
                   && key_path::is_indexed_segment(path.segments()[container.size()]))
                    continue;

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
                if(strains.find(selection.value()) == strains.end())
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

            if(auto r = relay_strain(strains.at(chosen), policy, Ld, Ls, wide_extend); !r)
                return r;

            // The strain's key value named the instance and was consumed; the
            // enforcer's identity-presence check is satisfied structurally.
            m_keyed_satisfied.push_back(container.str());
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
        if(m_schema.surface().empty())
            return {};

        schema_validation checked = schema_enforcer::validate(m_schema, m_building,
                                                              m_keyed_satisfied);
        if(checked)
            return {};

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
                    continue; // handled by the direct path branch below
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
    config freeze() const
    {
        std::map<std::string, std::string> owned;
        for(const key_path &path : m_building.paths())
        {
            if(const value *v = m_building.find(path))
                owned.emplace(path.str(), std::string(v->text()));
        }
        return config(std::move(owned), m_typed, m_provenance);
    }

    // Sets the pass-2 reference substitution budget. 0 maps to the engine default (never zero-cap).
    void set_reference_budget(std::size_t budget) noexcept
    {
        if(budget != 0)
            m_reference_budget = budget;
    }

private:
    // Re-lays one strain's keyed entries onto the unified (key-stripped) paths.
    // All repeated-path entries are indexed scalars; no collection branch needed.
    // Ordinal segments in the keyed path are preserved; only key-value segments
    // (transient instance selectors) are stripped.
    //
    // Flat-override wins wholesale: for a repeated path, if any existing indexed
    // entry at the unified canonical base has a higher rank than the keyed entry
    // being relayed, the entire repeated collection from the keyed source is
    // displaced (not just the individual slot). This ensures a flat override at
    // higher rank replaces the whole collection, not just [0].
    expected<void, resolve_fold_error>
    relay_strain(const std::vector<key_path> &keyed_paths,
                 strain_scope_policy policy, std::size_t Ld, std::size_t Ls,
                 bool wide_extend = false)
    {
        const schema_projection proj = m_schema.projection();

        // Canonical bases of repeated paths that are fully displaced by a
        // higher-rank flat override. Populated on first relay attempt per base.
        std::set<std::string> displaced_bases;

        for(const key_path &keyed : keyed_paths)
        {
            const origin *from = m_provenance.of(keyed.str());
            std::size_t entry_rank = from != nullptr ? from->rank : 0;
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

            // Compute the unified path: strip key segments but PRESERVE ordinal
            // segments so indexed repeated-leaf instances keep their ordinals.
            const std::string unified_str = relay_canonical(keyed);
            auto unified = key_path::parse(unified_str);
            if(!unified)
                continue;

            const value *v = m_building.find(keyed);
            if(v == nullptr)
            {
                // Nothing to relay (path exists in bucket but no value).
                m_building.remove(keyed);
                m_provenance.forget(keyed.str());
                continue;
            }

            // For indexed unified paths, check whether the canonical base (the
            // repeated field without the ordinal) is already wholly displaced by
            // a higher-rank override. A higher-rank entry at ANY indexed slot of
            // the same canonical base means the flat source replaced the entire
            // collection, so no relay entry should be written.
            const bool unified_is_indexed = [&]() {
                for(const auto &seg : unified.value().segments())
                    if(key_path::is_indexed_segment(seg))
                        return true;
                return false;
            }();
            if(unified_is_indexed)
            {
                // Canonical base: strip ordinals from all segments.
                const std::string canonical_base = m_schema.canonical_text(unified.value());
                if(displaced_bases.count(canonical_base))
                {
                    // Already determined the whole base is displaced; skip.
                    m_building.remove(keyed);
                    m_provenance.forget(keyed.str());
                    continue;
                }
                // Check: is any existing entry for the canonical base at higher rank?
                bool base_displaced = false;
                for(const key_path &bp : m_building.paths())
                {
                    if(m_schema.canonical_text(bp) != canonical_base)
                        continue;
                    const origin *bpfrom = m_provenance.of(bp.str());
                    if(bpfrom != nullptr && bpfrom->rank > entry_rank)
                    {
                        base_displaced = true;
                        break;
                    }
                }
                if(base_displaced)
                {
                    displaced_bases.insert(canonical_base);
                    m_building.remove(keyed);
                    m_provenance.forget(keyed.str());
                    continue;
                }
            }

            // Check if the unified path is already occupied by a higher-rank value.
            const origin *at = m_provenance.of(unified_str);
            const bool displaced = at != nullptr && at->rank > entry_rank;
            if(displaced)
            {
                // D-01: a pkey leaf is authoritative and read-only. If the unified
                // path IS the pkey leaf of its parent container and a higher-rank flat
                // entry occupies it, that is a loud layering error, not a silent skip.
                const auto slash = unified_str.rfind('/');
                if(slash != std::string::npos)
                {
                    const std::string_view parent = std::string_view(unified_str).substr(0, slash);
                    const std::string_view leaf   = std::string_view(unified_str).substr(slash + 1);
                    const std::string *pkey = proj.key_of(parent);
                    if(pkey != nullptr && *pkey == leaf)
                        return unexpected(error{errc::layering_violation,
                            nucleus::format(
                                "identity field '{}' is read-only; "
                                "source at rank {} cannot override it",
                                unified_str, at->rank)});
                }
            }
            else
            {
                m_building.set(unified.value(), *v);
                if(from != nullptr)
                    m_provenance.record(unified_str, *from);
            }
            m_building.remove(keyed);
            m_provenance.forget(keyed.str());
        }
        return {};
    }

    // Computes the unified relay path for a keyed+possibly-indexed path:
    // strips key-value segments (transient primary-key values) but preserves
    // ordinal segments (indexed repeated leaves keep their [N] suffix).
    std::string relay_canonical(const key_path &path) const
    {
        std::string canonical;
        for(std::size_t i = 0; i < path.segments().size(); ++i)
        {
            const std::string &segment = path.segments()[i];
            std::string extended = canonical;
            if(!extended.empty())
                extended += key_path::separator;
            extended += segment;

            // Indexed segments (e.g. "tags[0]"): keep as-is (preserve ordinal).
            if(key_path::is_indexed_segment(segment))
            {
                canonical = std::move(extended);
                continue;
            }

            // Key-value segment: the schema says this extended path is a keyed
            // instance path one level past the keyed container. Skip the key value.
            const auto canonical_kp = key_path::parse(canonical);
            if(canonical_kp.has_value())
            {
                // Build a fake "extended" key_path to check keyed_instance_path.
                const auto ext_kp = key_path::parse(extended);
                if(ext_kp.has_value()
                   && m_schema.keyed_instance_path(canonical_kp.value(), ext_kp.value()))
                    continue;
            }

            canonical = std::move(extended);
        }
        return canonical;
    }

    const schema_registry &m_schema;
    const tokenizer_registry &m_tokenizer;
    const converter_registry &m_converters;

    std::size_t m_reference_budget = default_reference_budget;

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
};

}

#endif

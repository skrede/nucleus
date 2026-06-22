#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/config_space.h"
#include "nucleus/query/schema_query_context.h"
#include "nucleus/registration_policy.h"

#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"
#include "nucleus/schema/capability_requirements.h"

#include "nucleus/resolve/chain_walker.h"
#include "nucleus/strain_scope.h"
#include "nucleus/resolve/resolution_context.h"

#include "nucleus/diagnostics/key_suggester.h"
#include "nucleus/diagnostics/conflict_report.h"

#include "nucleus/tokenizer/builtin_tokenizers.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/tokenizer/tree_tokenizer_registry.h"

#include <map>
#include <span>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <filesystem>
#include <string_view>

namespace nucleus {

// The three flat sibling registries + the host registration policy + the
// claim/conflict ledger. Owned by the builder while configurable and copied into
// the sealed space by build(); the same value type backs expand()'s deep copy.
// This is the single place the registries are owned; they hold no references to one
// another. Value-copyable: all three registries copy their entries and the ledger is
// plain maps, so a member-wise copy is a deep copy. The policy is a shared_ptr to
// host-owned behavior (not mutable state that can diverge), so sharing it across a
// base and its expand()-derived builder is intentional -- NO shared_ptr links the
// config_space objects themselves.
struct space_core
{
    // The generic core tokenizers are MECHANISM, not policy: ${env.*} and
    // ${string.*} carry no host vocabulary, so they install by default. Without
    // them a host gets no token expansion at all (the fold fails loudly on every
    // unresolved ${...}). Host vocabulary is the host's to inject via install_tokenizer.
    space_core()
    {
        owner_token core;
        tokenizer.add(make_env_tokenizer(), core);
        tokenizer.add(make_string_tokenizer(), core);
    }

    registration_result review(registration_kind kind, const owner_token &owner)
    {
        registration_request request{kind, owner};
        policy_verdict verdict = m_policy->review(request);
        if(!verdict.accepted())
            return unexpected(error{errc::rejected_registration, verdict.reason()});
        return registration_ok();
    }

    // Records that `owner` claimed `key_path` and, on a second-or-later claim of the
    // same path, builds/extends a non-adjudicating conflict_report naming every
    // claimant. The core surfaces who claimed what WITHOUT picking a winner.
    void note_claim(const std::string &key_path, registration_kind kind, const owner_token &owner)
    {
        std::vector<claimant> &claims = m_claims[key_path];
        claims.push_back(claimant{
            nucleus::format("{} registration of '{}'", to_string(kind), key_path), owner});
        if(claims.size() < 2)
            return;

        conflict_report report(key_path);
        for(const claimant &c : claims)
            report.add(c);
        m_conflicts.insert_or_assign(key_path, std::move(report));
    }

    std::vector<conflict_report> conflicts() const
    {
        std::vector<conflict_report> out;
        out.reserve(m_conflicts.size());
        for(const auto &[_, report] : m_conflicts)
            out.push_back(report);
        return out;
    }

    std::string name;
    schema_registry schema;
    tokenizer_registry tokenizer;
    tree_tokenizer_registry tree_tokenizer;
    converter_registry converters;
    std::shared_ptr<registration_policy> m_policy = std::make_shared<registration_policy>();

    // Per-path claim ledger and the conflict reports it produces. Keyed by claimed
    // key path so a third claim extends the same report. Surfaced, never adjudicated.
    std::map<std::string, std::vector<claimant>> m_claims;
    std::map<std::string, conflict_report> m_conflicts;
};

// The mutable builder's state: the shared core plus the spent flag.
class config_space_builder::impl : public space_core
{
public:
    bool built = false;
};

// The sealed space's state: the shared core, default-constructed (core tokenizers
// installed) or copied from a builder's core by build().
class config_space::impl : public space_core
{
public:
    impl() = default;
    explicit impl(const space_core &state) : space_core(state) {}
};

namespace {

// D-04: pkey tag must not shadow a builtin category or a pass-2 scheme head.
// A collision is a loud build-time error — the schema author renames the element.
inline bool is_reserved_tree_tokenizer_name(std::string_view name) noexcept
{
    return name == "env"  || name == "string"
        || name == "abs"  || name == "rel"
        || name == "scope"|| name == "file"
        || name == "dir"  || name == "self";
}

// Builds the auto-named pkey tree tokenizer for identity element `el`.
// Category = the container's last segment (D-01); resolver reads the sliced
// keyspace anchored to the pkey container (D-02), with a precise D-03 diagnostic
// when no instance is in scope post-slice.
tree_tokenizer make_pkey_tree_tokenizer(const schema_element &el)
{
    std::string category = std::string(key_path::base_name(el.container().leaf()));
    key_path pkey_container = el.container();
    std::string identity_field = el.name;

    return tree_tokenizer(
        std::move(category),
        [pkey_container, identity_field](const tree_access &access) -> token_result
        {
            // D-03: verify an instance is selected by checking the identity leaf.
            key_path identity_path = pkey_container.child(identity_field);
            if(access.building.find(identity_path) == nullptr)
                return unexpected(resolve_error(resolve_errc::missing_field,
                    nucleus::format("${{{}}} requires a selected primary-key instance; "
                                    "this configuration has none in scope",
                                    std::string(access.category) + "." +
                                    std::string(access.field_name))));

            // D-02: resolve the requested field within the pkey container.
            key_path field_path = pkey_container.child(std::string(access.field_name));
            const value *v = access.building.find(field_path);
            if(v == nullptr)
                return unexpected(resolve_error(resolve_errc::missing_field,
                    nucleus::format("${{{}}} has no field '{}' in the selected instance",
                                    access.category, access.field_name)));

            return std::string(v->text());
        });
}

// The state-machine guard: mutating the builder is only legal until build() seals
// it. An attempt after build() is rejected with a reason naming the operation that
// was actually attempted -- the lifecycle enforced, not merely documented.
registration_result reject_if_built(bool built, std::string_view what)
{
    if(built)
        return unexpected(error{errc::sealed_builder, nucleus::format(
            "{} is not allowed: the builder has already been built", what)});
    return registration_ok();
}

// Assembles the fold handles under the unified precedence scheme: the
// inheritance chain (if any) sits at the BASE, occupying the lowest ranks
// [0, m) base-first so the within-chain order (base below, derived above) is
// preserved; every source_stack handle then sits ABOVE the whole chain at rank
// m + index, so any stack source (env, argv, runtime, a document in the stack) overrides
// the document base. Cross-source precedence is carried entirely by rank; the
// within-chain layering is carried by the explicit inheritance_layer ordinal
// (equal to the chain index), which the slice step keys its re-open rules on.
// `entries` is an out-parameter the caller owns: the chain sources must outlive
// the fold. Returns the assembled handles or a chain-expansion error.
expected<std::vector<resolution_context::layered_handle>, error>
assemble_handles(const space_core &state,
                 source_stack &stack,
                 const load_options &options,
                 std::vector<chain_walker::chain_entry> &entries)
{
    // Expand the inheritance chain first so the stack handles can be ranked
    // above it. expand() returns entries base-first (index 0 = deepest ancestor).
    if(!options.document_paths.empty() && options.make_document)
    {
        const schema_projection projection = state.schema.projection();
        auto expanded = chain_walker::expand(options.document_paths, options.make_document,
                                             projection, options.inherit);
        if(!expanded)
            return unexpected(std::move(expanded).error());
        entries = std::move(expanded).value();
    }

    std::span<source_handle> layers = stack.layers();

    std::vector<resolution_context::layered_handle> handles;
    handles.reserve(entries.size() + layers.size());

    // The chain occupies the base ranks [0, m), base-first; each entry carries
    // its inheritance-chain layer ordinal equal to its chain index.
    for(std::size_t i = 0; i < entries.size(); ++i)
        handles.push_back({&entries[i].src, i,
                           nucleus::format("path:{}", entries[i].path), {}, i,
                           std::filesystem::path(entries[i].path)});

    // Stack handles sit ABOVE the whole chain: rank = chain size + stack index.
    // Their label keeps the bare stack index so provenance reads stack[N].
    const std::size_t base_offset = entries.size();
    for(std::size_t i = 0; i < layers.size(); ++i)
        handles.push_back({&layers[i], base_offset + i,
                           nucleus::format("stack[{}]", i), {}, std::nullopt,
                           std::nullopt});

    return handles;
}

// Gate for the handle-based fold path. Reads capabilities from each
// layered_handle without pulling; consistent with the gate for the pointer-based path.
gate_result gate_assembled_handles(
    const schema_registry &schema,
    std::span<resolution_context::layered_handle> layers,
    log_sink &log)
{
    std::vector<std::pair<std::string, capability_descriptor>> descriptors;
    descriptors.reserve(layers.size());
    for(const resolution_context::layered_handle &lh : layers)
        descriptors.emplace_back(lh.label, lh.handle->capabilities());
    return gate_stack("schema", descriptors, derive_capability_requirements(schema.elements()), log);
}

}

// --- config_space_builder -------------------------------------------

config_space_builder::config_space_builder()
    : m_impl(std::make_unique<impl>())
{
}

config_space_builder::~config_space_builder() = default;

config_space_builder::config_space_builder(config_space_builder &&) noexcept = default;

config_space_builder &
config_space_builder::operator=(config_space_builder &&) noexcept = default;

registration_result
config_space_builder::set_registration_policy(std::shared_ptr<registration_policy> policy)
{
    if(auto guard = reject_if_built(m_impl->built, "set_registration_policy"); !guard)
        return guard;
    m_impl->m_policy = policy ? std::move(policy)
                              : std::make_shared<registration_policy>();
    return registration_ok();
}

registration_result config_space_builder::register_schema(std::string key_path, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_schema"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    m_impl->note_claim(key_path, registration_kind::schema, owner);
    m_impl->schema.add(schema_spec{std::move(key_path)}, std::move(owner));
    return registration_ok();
}

registration_result config_space_builder::register_element(schema_element element, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_element"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    // D-04: identity elements whose container tag collides with a reserved name
    // would produce an unusable auto-registered tokenizer — reject loudly before
    // schema.attach() so the error reaches the host at schema-build time.
    if(element.identity && !element.container().empty())
    {
        std::string category = std::string(key_path::base_name(element.container().leaf()));
        if(is_reserved_tree_tokenizer_name(category))
            return unexpected(error{errc::rejected_registration,
                nucleus::format("tree tokenizer category '{}' collides with a reserved name; "
                                "rename the schema element", category)});
    }
    // The element's declared path is the key it claims; record it for conflict
    // surfacing before attach consumes the element.
    const std::string claimed = element.declared_path().str();
    // attach() enforces referential integrity; surface its rejection verbatim.
    if(auto attached = m_impl->schema.attach(std::move(element)); !attached)
        return unexpected(error{errc::rejected_registration, std::move(attached).error()});
    m_impl->note_claim(claimed, registration_kind::schema, owner);
    return registration_ok();
}

registration_result config_space_builder::register_constraint_group(constraint_group group,
                                                                     owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_constraint_group"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    if(auto attached = m_impl->schema.attach_constraint_group(std::move(group)); !attached)
        return unexpected(error{errc::rejected_registration, std::move(attached).error()});
    return registration_ok();
}

registration_result config_space_builder::register_identity_group(identity_group_spec group,
                                                                  owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_identity_group"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    // Reserved-prefix carve-out: a namespace name colliding with a builtin (a
    // reserved tree-tokenizer category or the engine's own 'nucleus' prefix) is
    // rejected so host identifiers can never shadow a builtin.
    if(is_reserved_tree_tokenizer_name(group.name) || group.name.rfind("nucleus", 0) == 0)
        return unexpected(error{errc::rejected_registration, nucleus::format(
            "identity group namespace '{}' collides with a reserved name", group.name)});
    if(auto attached = m_impl->schema.attach_identity_group(std::move(group)); !attached)
        return unexpected(error{errc::rejected_registration, std::move(attached).error()});
    return registration_ok();
}

registration_result config_space_builder::register_tokenizer(std::string name, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_tokenizer"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(tokenizer(std::move(name), {}, {}, nullptr), std::move(owner));
    return registration_ok();
}

registration_result config_space_builder::install_tokenizer(tokenizer tok, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "install_tokenizer"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(std::move(tok), std::move(owner));
    return registration_ok();
}

registration_result config_space_builder::install_tree_tokenizer(tree_tokenizer tok,
                                                                   owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "install_tree_tokenizer"); !guard)
        return guard;
    if(is_reserved_tree_tokenizer_name(tok.category()))
        return unexpected(error{errc::rejected_registration,
            nucleus::format("tree tokenizer category '{}' collides with a reserved name; "
                            "rename the schema element", tok.category())});
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    // D-05: host shadows the auto-registered pkey tokenizer for this category —
    // last-registration-wins is correct; wire a log_sink to space_core to emit
    // a debug-level message here when that seam is added.
    m_impl->tree_tokenizer.add(std::move(tok), std::move(owner));
    return registration_ok();
}

registration_result config_space_builder::register_converter(
    std::type_index id,
    std::function<expected<std::any, std::string>(std::string_view)> conv,
    owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_converter"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::converter, owner); !verdict)
        return verdict;
    m_impl->converters.add(id, std::move(conv));
    return registration_ok();
}

config_space_builder &config_space_builder::name(std::string space_name)
{
    m_impl->name = std::move(space_name);
    return *this;
}

std::size_t config_space_builder::schema_count() const noexcept { return m_impl->schema.size(); }

std::size_t config_space_builder::tokenizer_count() const noexcept { return m_impl->tokenizer.size(); }

std::size_t config_space_builder::converter_count() const noexcept { return m_impl->converters.size(); }

std::vector<conflict_report> config_space_builder::conflicts() const { return m_impl->conflicts(); }

config_space config_space_builder::build()
{
    // D-07: auto-register a pkey tree tokenizer for every identity element.
    // D-05: if the host already registered a tokenizer for this category, skip
    // auto-registration so the host's registration wins (last-registration-wins
    // is implemented by the host calling install_tree_tokenizer before build()).
    // Reserved names cannot reach here — register_element is the enforcement gate.
    owner_token core;
    for(const schema_element &el : m_impl->schema.elements())
    {
        if(!el.identity || el.container().empty())
            continue;
        std::string category = std::string(key_path::base_name(el.container().leaf()));
        if(m_impl->tree_tokenizer.find(category) != nullptr)
            continue; // D-05: host shadow wins
        m_impl->tree_tokenizer.add(make_pkey_tree_tokenizer(el), core);
    }

    // Infallible: copy the core (deep copy of the three registries + ledger; the
    // policy shared_ptr is shared host-owned behavior) into the sealed product and
    // mark the builder spent. After this, every mutating call is a loud error.
    m_impl->built = true;
    auto sealed = std::make_unique<config_space::impl>(
        static_cast<const space_core &>(*m_impl));
    return config_space(std::move(sealed));
}

// --- config_space (sealed) ------------------------------------------

config_space::config_space() : m_impl(std::make_unique<impl>()) {}

config_space::config_space(std::unique_ptr<impl> sealed) : m_impl(std::move(sealed)) {}

config_space::~config_space() = default;

config_space::config_space(const config_space &other)
    : m_impl(other.m_impl ? std::make_unique<impl>(*other.m_impl) : nullptr)
{
}

config_space &config_space::operator=(const config_space &other)
{
    if(this != &other)
        m_impl = other.m_impl ? std::make_unique<impl>(*other.m_impl) : nullptr;
    return *this;
}

config_space::config_space(config_space &&) noexcept = default;

config_space &config_space::operator=(config_space &&) noexcept = default;

std::size_t config_space::schema_count() const noexcept { return m_impl->schema.size(); }

std::size_t config_space::tokenizer_count() const noexcept { return m_impl->tokenizer.size(); }

std::size_t config_space::converter_count() const noexcept { return m_impl->converters.size(); }

std::vector<conflict_report> config_space::conflicts() const { return m_impl->conflicts(); }

std::string_view config_space::space_name() const noexcept { return m_impl->name; }

std::string config_space::generate_completion(shell which, std::string_view prog,
                                                     const cli_delimiter &delimiter,
                                                     const key_path &anchor,
                                                     std::string_view space_name) const
{
    // Project the sealed schema through the free generator. Only the script string
    // crosses the boundary; the registry stays encapsulated.
    return nucleus::generate_completion(which, m_impl->schema, prog, delimiter, anchor, space_name);
}

std::span<const schema_element> config_space::schema_elements() const
{
    // Project the sealed schema's declared elements as a read-only view; the
    // registry stays encapsulated and no format knowledge enters core.
    return m_impl->schema.elements();
}

schema_query_context config_space::query_context() const
{
    // Build the transient owner index from the claim ledger: for each schema
    // element, the first claimant's token is the canonical owner for that path.
    std::map<std::string, owner_token, std::less<>> owner_map;
    for(const schema_element &el : m_impl->schema.elements())
    {
        const std::string dp = el.declared_path().str();
        auto it = m_impl->m_claims.find(dp);
        if(it != m_impl->m_claims.end() && !it->second.empty())
            owner_map.emplace(dp, it->second[0].owner);
    }
    return schema_query_context{m_impl->schema.elements(), std::move(owner_map),
                                m_impl->schema.identity_groups()};
}

config_space_builder config_space::expand() const
{
    // Deep copy: all three registries + ledger are value-copied into a fresh builder
    // (the policy shared_ptr is shared host-owned behavior). NO shared_ptr links the
    // base and the derived builder, so building or mutating one never affects the
    // other.
    config_space_builder builder;
    static_cast<space_core &>(*builder.m_impl) = static_cast<const space_core &>(*m_impl);
    builder.m_impl->built = false;
    return builder;
}

// --- recognizer_of ----------------------------------------------------------

key_recognizer recognizer_of(const config_space &space)
{
    // Captures the space's schema registry by pointer; the recognizer is valid
    // for as long as the space outlives it. Uses the ordinal-aware variant so
    // CLI plain-ordinal paths (D-09) are accepted: "cluster/node/0/endpoint/port"
    // is recognized when "cluster/node" is a repeated container and
    // "cluster/node/endpoint/port" is declared.
    const schema_registry *schema = &space.m_impl->schema;
    return [schema](const key_path &path)
    { return schema->recognizes_with_ordinal(path); };
}

// --- load(space, source_stack, load_options) --------------------------------

load_result load_config(const config_space &space,
                 source_stack &stack,
                 const load_options &options)
{
    const space_core &state = *space.m_impl;

    // Chain entries own the folded document sources; they must outlive the fold.
    std::vector<chain_walker::chain_entry> entries;
    auto assembled = assemble_handles(state, stack, options, entries);
    if(!assembled)
        return unexpected(std::move(assembled).error());
    std::vector<resolution_context::layered_handle> handles = std::move(assembled).value();

    log_sink default_log;
    if(auto gated = gate_assembled_handles(state.schema, handles, default_log); !gated)
        return unexpected(std::move(gated).error());

    resolution_context ctx(state.schema, state.tokenizer, state.converters,
                           state.tree_tokenizer);
    if(auto folded = ctx.fold(handles); !folded)
        return unexpected(std::move(folded).error());
    if(auto merged = ctx.merge_keyed_collections(); !merged)
        return unexpected(std::move(merged).error());
    if(auto sliced = ctx.slice(options.selection, options.scope); !sliced)
        return unexpected(std::move(sliced).error());
    ctx.set_reference_budget(options.reference_budget);
    if(auto refs = ctx.resolve_references(); !refs)
        return unexpected(std::move(refs).error());
    if(auto checked = ctx.validate(); !checked)
        return unexpected(std::move(checked).error());
    if(auto converted = ctx.convert(); !converted)
        return unexpected(std::move(converted).error());
    return ctx.freeze();
}

load_result load_config(const config_space &space,
                 source_stack &&stack,
                 const load_options &options)
{
    return load_config(space, stack, options);
}

// --- check_capabilities(space, source_stack, load_options) ------------------

gate_result check_capabilities(const config_space &space,
                                const source_stack &stack,
                                const load_options &options)
{
    const space_core &state = *space.m_impl;

    // Mirror load()'s precedence scheme (chain at the base ranks, stack above)
    // without touching the stack: the gate needs only labels and capability
    // descriptors, both readable through the const surface, so a pre-flight
    // leaves the stack intact for the load() that follows.
    std::vector<chain_walker::chain_entry> entries;
    if(!options.document_paths.empty() && options.make_document)
    {
        const schema_projection projection = state.schema.projection();
        auto expanded = chain_walker::expand(options.document_paths, options.make_document,
                                             projection, options.inherit);
        if(!expanded)
            return unexpected(std::move(expanded).error());
        entries = std::move(expanded).value();
    }

    std::vector<std::pair<std::string, capability_descriptor>> descriptors;
    const std::span<const source_handle> layers = stack.layers();
    descriptors.reserve(entries.size() + layers.size());
    for(const chain_walker::chain_entry &entry : entries)
        descriptors.emplace_back(nucleus::format("path:{}", entry.path),
                                 entry.src.capabilities());
    for(std::size_t i = 0; i < layers.size(); ++i)
        descriptors.emplace_back(nucleus::format("stack[{}]", i), layers[i].capabilities());

    log_sink default_log;
    return gate_stack("schema", descriptors,
                      derive_capability_requirements(state.schema.elements()), default_log);
}

}

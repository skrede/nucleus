#include "nucleus/format.h"
#include "nucleus/configuration_space.h"
#include "nucleus/registration_policy.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/schema/converter_registry.h"
#include "nucleus/schema/capability_requirements.h"

#include "nucleus/configuration_source/configuration_source_registry.h"

#include "nucleus/configuration_source/env/env_source.h"
#include "nucleus/configuration_source/argv/argv_source.h"

#include "nucleus/entry/chain_walker.h"
#include "nucleus/entry/strain_scope.h"
#include "nucleus/entry/resolution_context.h"

#include "nucleus/diagnostics/key_suggester.h"
#include "nucleus/diagnostics/conflict_report.h"

#include "nucleus/tokenizer/builtin_tokenizers.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <map>
#include <span>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <string_view>

namespace nucleus {

// The four flat sibling registries + the host registration policy + the
// claim/conflict ledger. Owned by the builder while configurable and copied into
// the sealed space by build(); the same value type backs expand()'s deep copy.
// This is the single place the registries are owned; they hold no references to one
// another. Value-copyable: all four registries copy their entries and the ledger is
// plain maps, so a member-wise copy is a deep copy. The policy is a shared_ptr to
// host-owned behavior (not mutable state that can diverge), so sharing it across a
// base and its expand()-derived builder is intentional -- NO shared_ptr links the
// configuration_space objects themselves.
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
            return unexpected(verdict.reason());
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

    [[nodiscard]] std::vector<conflict_report> conflicts() const
    {
        std::vector<conflict_report> out;
        out.reserve(m_conflicts.size());
        for(const auto &[_, report] : m_conflicts)
            out.push_back(report);
        return out;
    }

    schema_registry schema;
    tokenizer_registry tokenizer;
    configuration_source_registry sources;
    converter_registry converters;
    std::shared_ptr<registration_policy> m_policy = std::make_shared<registration_policy>();

    // Per-path claim ledger and the conflict reports it produces. Keyed by claimed
    // key path so a third claim extends the same report. Surfaced, never adjudicated.
    std::map<std::string, std::vector<claimant>> m_claims;
    std::map<std::string, conflict_report> m_conflicts;
};

// The mutable builder's state: the shared core plus the spent flag.
class configuration_space_builder::impl : public space_core
{
public:
    bool built = false;
};

// The sealed space's state: the shared core, default-constructed (core tokenizers
// installed) or copied from a builder's core by build().
class configuration_space::impl : public space_core
{
public:
    impl() = default;
    explicit impl(const space_core &state) : space_core(state) {}
};

namespace {

// Maps a document's position in a path list onto a precedence rank that is always
// STRICTLY BELOW argv. The first path is the base; each later path overlays the
// previous one, but the whole band is clamped to the overlay rank so that no
// document -- however many were supplied -- can ever tie or outrank argv.
[[nodiscard]] std::size_t document_rank(std::size_t index)
{
    const auto base    = static_cast<std::size_t>(layer_rank::base);
    const auto overlay = static_cast<std::size_t>(layer_rank::overlay);
    const std::size_t raw = base + index;
    return raw < overlay ? raw : overlay;
}

// The state-machine guard: mutating the builder is only legal until build() seals
// it. An attempt after build() is rejected with a reason naming the operation that
// was actually attempted -- the lifecycle enforced, not merely documented.
[[nodiscard]] registration_result reject_if_built(bool built, std::string_view what)
{
    if(built)
        return unexpected(nucleus::format(
            "{} is not allowed: the builder has already been built", what));
    return registration_ok();
}

// Gate for the handle-based fold path. Reads capabilities from each
// layered_handle without pulling; consistent with the gate for the pointer-based path.
[[nodiscard]] gate_result gate_assembled_handles(
    const schema_registry &schema,
    std::span<resolution_context::layered_handle> layers,
    log_sink &log)
{
    std::vector<std::pair<std::string, capability_descriptor>> descriptors;
    descriptors.reserve(layers.size());
    for(const resolution_context::layered_handle &lh : layers)
        descriptors.emplace_back(lh.label, lh.handle->capabilities());
    return gate_stack("schema", descriptors, derive_capability_requirements(schema), log);
}

// Thin configuration_source subclass that owns a source_handle and forwards all
// calls. Used to adapt a source_handle back into the chain_walker's factory_fn
// contract (unique_ptr<configuration_source>) for document inheritance walking.
class handle_as_source final : public configuration_source
{
public:
    explicit handle_as_source(source_handle h) : m_handle(std::move(h)) {}

    [[nodiscard]] capability_descriptor capabilities() const override
    { return m_handle.capabilities(); }
    void apply_projection(const schema_projection &p) override
    { m_handle.apply_projection(p); }
    [[nodiscard]] inherit_declaration inheritance() const override
    { return m_handle.inheritance(); }
    [[nodiscard]] configuration_source_result pull() override
    { return m_handle.pull(); }

private:
    source_handle m_handle;
};

}

// --- configuration_space_builder -------------------------------------------

configuration_space_builder::configuration_space_builder()
    : m_impl(std::make_unique<impl>())
{
}

configuration_space_builder::~configuration_space_builder() = default;

configuration_space_builder::configuration_space_builder(configuration_space_builder &&) noexcept = default;

configuration_space_builder &
configuration_space_builder::operator=(configuration_space_builder &&) noexcept = default;

registration_result
configuration_space_builder::set_registration_policy(std::shared_ptr<registration_policy> policy)
{
    if(auto guard = reject_if_built(m_impl->built, "set_registration_policy"); !guard)
        return guard;
    m_impl->m_policy = policy ? std::move(policy)
                              : std::make_shared<registration_policy>();
    return registration_ok();
}

registration_result configuration_space_builder::register_schema(std::string key_path, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_schema"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    m_impl->note_claim(key_path, registration_kind::schema, owner);
    m_impl->schema.add(schema_spec{std::move(key_path)}, std::move(owner));
    return registration_ok();
}

registration_result configuration_space_builder::register_element(schema_element element, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_element"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    // The element's declared path is the key it claims; record it for conflict
    // surfacing before attach consumes the element.
    const std::string claimed = element.declared_path().str();
    // attach() enforces referential integrity; surface its rejection verbatim.
    if(auto attached = m_impl->schema.attach(std::move(element)); !attached)
        return unexpected(std::move(attached).error());
    m_impl->note_claim(claimed, registration_kind::schema, owner);
    return registration_ok();
}

registration_result configuration_space_builder::register_tokenizer(std::string name, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_tokenizer"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(tokenizer(std::move(name), {}, {}, nullptr), std::move(owner));
    return registration_ok();
}

registration_result configuration_space_builder::install_tokenizer(tokenizer tok, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "install_tokenizer"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(std::move(tok), std::move(owner));
    return registration_ok();
}

registration_result configuration_space_builder::register_source(std::string name, owner_token owner)
{
    if(auto guard = reject_if_built(m_impl->built, "register_source"); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::configuration_source, owner); !verdict)
        return verdict;
    m_impl->sources.add(configuration_source_spec{std::move(name)}, std::move(owner));
    return registration_ok();
}

registration_result configuration_space_builder::register_converter(
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

std::size_t configuration_space_builder::schema_count() const noexcept { return m_impl->schema.size(); }

std::size_t configuration_space_builder::tokenizer_count() const noexcept { return m_impl->tokenizer.size(); }

std::size_t configuration_space_builder::source_count() const noexcept { return m_impl->sources.size(); }

std::size_t configuration_space_builder::converter_count() const noexcept { return m_impl->converters.size(); }

std::vector<conflict_report> configuration_space_builder::conflicts() const { return m_impl->conflicts(); }

configuration_space configuration_space_builder::build()
{
    // Infallible: copy the core (deep copy of the four registries + ledger; the
    // policy shared_ptr is shared host-owned behavior) into the sealed product and
    // mark the builder spent. After this, every mutating call is a loud error.
    m_impl->built = true;
    auto sealed = std::make_unique<configuration_space::impl>(
        static_cast<const space_core &>(*m_impl));
    return configuration_space(std::move(sealed));
}

// --- configuration_space (sealed) ------------------------------------------

configuration_space::configuration_space() : m_impl(std::make_unique<impl>()) {}

configuration_space::configuration_space(std::unique_ptr<impl> sealed) : m_impl(std::move(sealed)) {}

configuration_space::~configuration_space() = default;

configuration_space::configuration_space(const configuration_space &other)
    : m_impl(other.m_impl ? std::make_unique<impl>(*other.m_impl) : nullptr)
{
}

configuration_space &configuration_space::operator=(const configuration_space &other)
{
    if(this != &other)
        m_impl = other.m_impl ? std::make_unique<impl>(*other.m_impl) : nullptr;
    return *this;
}

configuration_space::configuration_space(configuration_space &&) noexcept = default;

configuration_space &configuration_space::operator=(configuration_space &&) noexcept = default;

std::size_t configuration_space::schema_count() const noexcept { return m_impl->schema.size(); }

std::size_t configuration_space::tokenizer_count() const noexcept { return m_impl->tokenizer.size(); }

std::size_t configuration_space::source_count() const noexcept { return m_impl->sources.size(); }

std::size_t configuration_space::converter_count() const noexcept { return m_impl->converters.size(); }

std::vector<conflict_report> configuration_space::conflicts() const { return m_impl->conflicts(); }

std::string configuration_space::generate_completion(shell which, std::string_view prog) const
{
    // Project the sealed schema through the free generator. Only the script string
    // crosses the boundary; the registry stays encapsulated.
    return nucleus::generate_completion(which, m_impl->schema, prog);
}

std::span<const schema_element> configuration_space::schema_elements() const
{
    // Project the sealed schema's declared elements as a read-only view; the
    // registry stays encapsulated and no format knowledge enters core.
    return m_impl->schema.elements();
}

configuration_space_builder configuration_space::expand() const
{
    // Deep copy: all four registries + ledger are value-copied into a fresh builder
    // (the policy shared_ptr is shared host-owned behavior). NO shared_ptr links the
    // base and the derived builder, so building or mutating one never affects the
    // other.
    configuration_space_builder builder;
    static_cast<space_core &>(*builder.m_impl) = static_cast<const space_core &>(*m_impl);
    builder.m_impl->built = false;
    return builder;
}

// --- recognizer_of ----------------------------------------------------------

key_recognizer recognizer_of(const configuration_space &space)
{
    // Captures the space's schema registry by pointer; the recognizer is valid
    // for as long as the space outlives it.
    const schema_registry *schema = &space.m_impl->schema;
    return [schema](const key_path &path) { return schema->recognizes(path); };
}

// --- new load(space, source_stack, load_options) ----------------------------

load_result load(const configuration_space &space,
                 source_stack stack,
                 const load_options &options)
{
    const space_core &state = *space.m_impl;

    // Chain entries own the folded document sources; they must outlive the fold.
    std::vector<chain_walker::chain_entry> entries;

    // Assign ascending ranks to the caller's source_stack handles (index = rank).
    // Later-listed handles have higher rank and win key contests.
    std::vector<resolution_context::layered_handle> handles;
    {
        std::span<source_handle> layers = stack.layers();
        handles.reserve(layers.size());
        for(std::size_t i = 0; i < layers.size(); ++i)
            handles.push_back({&layers[i], i, nucleus::format("stack[{}]", i), {}});
    }

    if(!options.document_paths.empty() && options.make_document)
    {
        // Adapt the source_handle-returning factory to the chain_walker's
        // unique_ptr<configuration_source> factory via handle_as_source.
        const auto &make_doc = options.make_document;
        chain_walker::factory_fn adapted = [&make_doc](const std::string &p)
            -> std::unique_ptr<configuration_source> {
            return std::make_unique<handle_as_source>(make_doc(p));
        };
        const schema_projection projection = state.schema.projection();
        auto expanded = chain_walker::expand(options.document_paths, adapted,
                                             projection, options.inherit);
        if(!expanded)
            return unexpected(std::move(expanded).error());
        entries = std::move(expanded).value();
        // Document entries get document_rank(i) values -- in the 200-899 band,
        // strictly above index-based stack ranks and strictly below argv rank (1000).
        // Source_stack handles that represent argv-like sources should be placed AFTER
        // documents in the stack so their index rank exceeds document_rank values; that
        // ordering is the caller's responsibility when composing the source_stack.
        const std::size_t doc_start = handles.size();
        handles.reserve(doc_start + entries.size());
        for(std::size_t i = 0; i < entries.size(); ++i)
            handles.push_back({&entries[i].src, document_rank(i),
                               nucleus::format("path:{}", entries[i].path), {}});
    }

    log_sink default_log;
    if(auto gated = gate_assembled_handles(state.schema, handles, default_log); !gated)
        return unexpected(std::move(gated).error());

    resolution_context ctx(state.schema, state.tokenizer, state.converters);
    if(auto folded = ctx.fold(handles); !folded)
        return unexpected(std::move(folded).error());
    if(auto sliced = ctx.slice(options.selection, options.scope); !sliced)
        return unexpected(std::move(sliced).error());
    if(auto checked = ctx.validate(); !checked)
        return unexpected(std::move(checked).error());
    if(auto converted = ctx.convert(); !converted)
        return unexpected(std::move(converted).error());
    return ctx.freeze();
}

// --- new check_capabilities(space, source_stack, load_options) --------------

gate_result check_capabilities(const configuration_space &space,
                                source_stack stack,
                                const load_options &options)
{
    const space_core &state = *space.m_impl;

    std::vector<chain_walker::chain_entry> entries;
    std::vector<resolution_context::layered_handle> handles;
    {
        std::span<source_handle> layers = stack.layers();
        handles.reserve(layers.size());
        for(std::size_t i = 0; i < layers.size(); ++i)
            handles.push_back({&layers[i], i, nucleus::format("stack[{}]", i), {}});
    }

    if(!options.document_paths.empty() && options.make_document)
    {
        const auto &make_doc = options.make_document;
        chain_walker::factory_fn adapted = [&make_doc](const std::string &p)
            -> std::unique_ptr<configuration_source> {
            return std::make_unique<handle_as_source>(make_doc(p));
        };
        const schema_projection projection = state.schema.projection();
        auto expanded = chain_walker::expand(options.document_paths, adapted,
                                             projection, options.inherit);
        if(!expanded)
            return unexpected(std::move(expanded).error());
        entries = std::move(expanded).value();
        handles.reserve(handles.size() + entries.size());
        for(std::size_t i = 0; i < entries.size(); ++i)
            handles.push_back({&entries[i].src, document_rank(i),
                               nucleus::format("path:{}", entries[i].path), {}});
    }

    log_sink default_log;
    return gate_assembled_handles(state.schema, handles, default_log);
}

// --- free load_configuration (thin adapter) ---------------------------------

load_result load_configuration(const configuration_space &space, const source_stack_options &options)
{
    // Borrow the sealed space's core by CONST reference; every mutable resolve
    // buffer below lives on this function's own stack, so concurrent calls on one
    // shared const space share nothing mutable.
    const space_core &state = *space.m_impl;

    // Build the handle-based fold input directly, preserving the exact layer_rank
    // values so stable_sort, doc_band_min, and slice() band checks are numerically
    // identical to the pre-refactor behavior.
    std::vector<resolution_context::layered_handle> handles;

    // Env source: owned here, wrapped directly into source_handle.
    source_stack env_stack;
    if(options.env)
    {
        env_stack.push_back(source_handle(env_source{options.env->entries}));
        handles.push_back({&env_stack.layers()[0],
                           static_cast<std::size_t>(layer_rank::env), "env", {}});
    }

    // Document sources via chain_walker: expanded first, ranked in document band.
    std::vector<chain_walker::chain_entry> entries;
    if(!options.document_paths.empty())
    {
        const schema_projection projection = state.schema.projection();
        auto expanded = chain_walker::expand(options.document_paths, options.make_document,
                                             projection, options.inherit);
        if(!expanded)
            return unexpected(std::move(expanded).error());
        entries = std::move(expanded).value();
        for(std::size_t i = 0; i < entries.size(); ++i)
            handles.push_back({&entries[i].src, document_rank(i),
                               nucleus::format("path:{}", entries[i].path), {}});
    }

    // Argv source: built as a value, configured in place, then moved into a handle.
    source_stack argv_stack;
    if(options.argv)
    {
        argv_source argv_src(options.argv->args);
        argv_src.policy(options.argv->policy);
        if(options.argv->log != nullptr)
            argv_src.log_to(*options.argv->log);
        if(options.argv->recognize_against_schema)
        {
            // The recognizer bridges to the schema surface; it captures the const
            // schema registry by reference and lives only for this load call.
            const schema_registry &schema = state.schema;
            argv_src.recognize_with([&schema](const key_path &path) { return schema.recognizes(path); });
        }
        argv_stack.push_back(source_handle(std::move(argv_src)));
        handles.push_back({&argv_stack.layers()[0],
                           static_cast<std::size_t>(layer_rank::argv), "argv", {}});
    }

    // Custom layers: borrowed pointers wrapped via source_ptr_adapter.
    std::vector<source_stack> custom_stacks;
    custom_stacks.reserve(options.custom_layers.size());
    for(const configuration_source_layer &layer : options.custom_layers)
    {
        if(layer.src == nullptr)
            continue;
        custom_stacks.emplace_back();
        // Wrap the borrowed pointer; the underlying source is owned by the caller.
        struct borrowed_adapter
        {
            configuration_source *ptr;
            [[nodiscard]] capability_descriptor capabilities() const { return ptr->capabilities(); }
            void apply_projection(const schema_projection &p) { ptr->apply_projection(p); }
            [[nodiscard]] inherit_declaration inheritance() const { return ptr->inheritance(); }
            [[nodiscard]] configuration_source_result pull() { return ptr->pull(); }
        };
        custom_stacks.back().push_back(source_handle(borrowed_adapter{layer.src}));
        handles.push_back({&custom_stacks.back().layers()[0],
                           layer.rank, layer.label, layer.owner});
    }

    log_sink default_log;
    log_sink &log = (options.argv && options.argv->log) ? *options.argv->log : default_log;
    if(auto gated = gate_assembled_handles(state.schema, handles, log); !gated)
        return unexpected(std::move(gated).error());

    resolution_context ctx(state.schema, state.tokenizer, state.converters);
    if(auto folded = ctx.fold(handles); !folded)
        return unexpected(std::move(folded).error());
    if(auto sliced = ctx.slice(options.selection, options.scope); !sliced)
        return unexpected(std::move(sliced).error());
    if(auto checked = ctx.validate(); !checked)
        return unexpected(std::move(checked).error());
    if(auto converted = ctx.convert(); !converted)
        return unexpected(std::move(converted).error());
    return ctx.freeze();
}

// --- check_capabilities (auto-gate pre-flight, legacy adapter) --------------

gate_result check_capabilities(const configuration_space &space, const source_stack_options &options)
{
    const space_core &state = *space.m_impl;

    std::vector<resolution_context::layered_handle> handles;

    source_stack env_stack;
    if(options.env)
    {
        env_stack.push_back(source_handle(env_source{options.env->entries}));
        handles.push_back({&env_stack.layers()[0],
                           static_cast<std::size_t>(layer_rank::env), "env", {}});
    }

    std::vector<chain_walker::chain_entry> entries;
    if(!options.document_paths.empty())
    {
        const schema_projection projection = state.schema.projection();
        auto expanded = chain_walker::expand(options.document_paths, options.make_document,
                                             projection, options.inherit);
        if(!expanded)
            return unexpected(std::move(expanded).error());
        entries = std::move(expanded).value();
        for(std::size_t i = 0; i < entries.size(); ++i)
            handles.push_back({&entries[i].src, document_rank(i),
                               nucleus::format("path:{}", entries[i].path), {}});
    }

    source_stack argv_stack;
    if(options.argv)
    {
        argv_source argv_src(options.argv->args);
        argv_src.policy(options.argv->policy);
        if(options.argv->log != nullptr)
            argv_src.log_to(*options.argv->log);
        argv_stack.push_back(source_handle(std::move(argv_src)));
        handles.push_back({&argv_stack.layers()[0],
                           static_cast<std::size_t>(layer_rank::argv), "argv", {}});
    }

    std::vector<source_stack> custom_stacks;
    custom_stacks.reserve(options.custom_layers.size());
    for(const configuration_source_layer &layer : options.custom_layers)
    {
        if(layer.src == nullptr)
            continue;
        custom_stacks.emplace_back();
        struct borrowed_adapter
        {
            configuration_source *ptr;
            [[nodiscard]] capability_descriptor capabilities() const { return ptr->capabilities(); }
            void apply_projection(const schema_projection &p) { ptr->apply_projection(p); }
            [[nodiscard]] inherit_declaration inheritance() const { return ptr->inheritance(); }
            [[nodiscard]] configuration_source_result pull() { return ptr->pull(); }
        };
        custom_stacks.back().push_back(source_handle(borrowed_adapter{layer.src}));
        handles.push_back({&custom_stacks.back().layers()[0],
                           layer.rank, layer.label, layer.owner});
    }

    log_sink default_log;
    log_sink &log = (options.argv && options.argv->log) ? *options.argv->log : default_log;
    return gate_assembled_handles(state.schema, handles, log);
}

}

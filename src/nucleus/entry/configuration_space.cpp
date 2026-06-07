#include "nucleus/format.h"
#include "nucleus/configuration_space.h"
#include "nucleus/registration_policy.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/schema_enforcer.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/source/source_registry.h"

#include "nucleus/source/argv/argv_source.h"

#include "nucleus/entry/resolution_context.h"

#include "nucleus/diagnostics/key_suggester.h"
#include "nucleus/diagnostics/conflict_report.h"

#include "nucleus/tokenizer/builtin_tokenizers.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <map>
#include <memory>
#include <vector>
#include <utility>

namespace nucleus {

// The facade composition-owns the three flat sibling registries plus the host
// registration policy. This is the single place the registries are owned; they
// hold no references to one another.
class configuration_space::impl
{
public:
    // The generic core tokenizers are MECHANISM, not policy: ${env.*}, ${uuid.*},
    // ${string.*} (and the scope file frame, handled inside the resolver) carry no
    // host vocabulary, so they are installed by default on construction. Without
    // this a host cannot get any token expansion through load()/resolve() at all,
    // because the fold fails loudly on every unresolved ${...}. The HOST tokenizer
    // module stays OPT-IN -- it pulls platform vocabulary and is registered only by
    // a host that links it and calls install_tokenizer(make_host_tokenizer()).
    impl()
    {
        owner_token core;
        tokenizer.add(make_env_tokenizer(), core);
        tokenizer.add(make_uuid_tokenizer(), core);
        tokenizer.add(make_string_tokenizer(), core);
    }

    registration_result review(registration_kind kind, const owner_token &owner)
    {
        registration_request request{kind, owner};
        policy_verdict verdict = m_policy->review(request);
        if(!verdict.accepted())
            return fail(verdict.reason());
        return registration_ok();
    }

    // Records that `owner` claimed `key_path` and, on a second-or-later claim of
    // the same path, builds/extends a non-adjudicating conflict_report naming
    // every claimant. The core surfaces who claimed what WITHOUT picking a winner
    // (mechanism, not policy): a host queries conflicts() and decides. A claim is
    // any path-bearing registration -- a schema path string or a typed element's
    // declared path. The location label is the claimed path plus the registration
    // surface; the opaque owner token travels for host adjudication.
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

    // The shared resolve path: build the transient borrowing context, fold the
    // stack (expand-then-layer with provenance), freeze the immutable result, and
    // transition the state machine. Enforces the no-resolve-after-resolve rule.
    load_result run_resolve(const source_stack &stack)
    {
        if(phase != facade_phase::configurable)
            return fail(std::string(
                "load/resolve is not allowed: the facade is already resolved"));

        resolution_context ctx(schema, tokenizer);
        if(auto folded = ctx.fold(stack); !folded)
            return fail(std::move(folded).error());

        // Keyed-container instances collapse onto the unified hierarchy BEFORE
        // the schema gates content: a primary-key value is transient resolution
        // state and never reaches validation or the frozen configuration. An
        // ambiguous fold (several strains, no selection) fails loudly here.
        if(auto sliced = ctx.slice(); !sliced)
            return fail(std::move(sliced).error());

        // The schema is the authority over CONTENT: a non-empty schema gates the
        // folded keyspace before it is frozen, so undeclared keys and missing
        // required/identity fields fail the resolve rather than silently shipping.
        if(auto checked = ctx.validate(); !checked)
            return fail(std::move(checked).error());

        configuration result = ctx.freeze();
        // The context (and every retained source buffer) is dropped here; the
        // frozen configuration holds only owned values.
        phase = facade_phase::resolved;
        return result;
    }

    schema_registry schema;
    tokenizer_registry tokenizer;
    source_registry sources;
    facade_phase phase = facade_phase::configurable;
    std::shared_ptr<registration_policy> m_policy = std::make_shared<registration_policy>();

    // Per-path claim ledger and the conflict reports it produces. Keyed by claimed
    // key path so a third claim extends the same report. Surfaced, never adjudicated.
    std::map<std::string, std::vector<claimant>> m_claims;
    std::map<std::string, conflict_report> m_conflicts;
};

configuration_space::configuration_space() : m_impl(std::make_unique<impl>()) {}

configuration_space::~configuration_space() = default;

configuration_space::configuration_space(configuration_space &&) noexcept = default;

configuration_space &configuration_space::operator=(configuration_space &&) noexcept = default;

void configuration_space::set_registration_policy(std::shared_ptr<registration_policy> policy)
{
    m_impl->m_policy = policy ? std::move(policy)
                              : std::make_shared<registration_policy>();
}

namespace {

// Maps a document's position in a path list onto a precedence rank that is always
// STRICTLY BELOW argv. The first path is the base; each later path overlays the
// previous one, but the whole band is clamped to the overlay rank so that no
// document -- however many were supplied -- can ever tie or outrank argv. The
// locked precedence "argv > overlay > base > env > defaults" demands argv win
// every config file unconditionally; a naive base+i would let a 3rd path tie argv
// and a 4th outrank it. Within the document band later still beats earlier because
// the fold is a stable sort and documents are added in list order.
[[nodiscard]] std::size_t document_rank(std::size_t index)
{
    const auto base    = static_cast<std::size_t>(layer_rank::base);
    const auto overlay = static_cast<std::size_t>(layer_rank::overlay);
    const std::size_t raw = base + index;
    return raw < overlay ? raw : overlay;
}

// The state-machine guard: registration is only legal while configurable. A
// registration attempted after resolve is rejected with a verbatim reason -- the
// two-phase lifecycle enforced, not merely documented.
[[nodiscard]] registration_result reject_if_resolved(facade_phase phase, registration_kind kind)
{
    if(phase != facade_phase::configurable)
        return fail(nucleus::format(
            "cannot register a {} after the facade has resolved", to_string(kind)));
    return registration_ok();
}

}

registration_result configuration_space::register_schema(std::string key_path, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::schema); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    m_impl->note_claim(key_path, registration_kind::schema, owner);
    m_impl->schema.add(schema_spec{std::move(key_path)}, std::move(owner));
    return registration_ok();
}

registration_result configuration_space::register_element(schema_element element, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::schema); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    // The element's declared path is the key it claims; record it for conflict
    // surfacing before attach consumes the element.
    const std::string claimed = element.declared_path().str();
    // attach() enforces referential integrity; surface its rejection verbatim.
    if(auto attached = m_impl->schema.attach(std::move(element)); !attached)
        return fail(std::move(attached).error());
    m_impl->note_claim(claimed, registration_kind::schema, owner);
    return registration_ok();
}

registration_result configuration_space::register_tokenizer(std::string name, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::tokenizer); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(tokenizer(std::move(name), {}, {}, nullptr), std::move(owner));
    return registration_ok();
}

registration_result configuration_space::install_tokenizer(tokenizer tok, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::tokenizer); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(std::move(tok), std::move(owner));
    return registration_ok();
}

registration_result configuration_space::register_source(std::string name, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::source); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::source, owner); !verdict)
        return verdict;
    m_impl->sources.add(source_spec{std::move(name)}, std::move(owner));
    return registration_ok();
}

std::size_t configuration_space::schema_count() const noexcept { return m_impl->schema.size(); }

std::size_t configuration_space::tokenizer_count() const noexcept { return m_impl->tokenizer.size(); }

std::size_t configuration_space::source_count() const noexcept { return m_impl->sources.size(); }

std::vector<conflict_report> configuration_space::conflicts() const { return m_impl->conflicts(); }

gate_result configuration_space::gate_capabilities(std::string_view consumer,
                                       std::string_view source_name,
                                       const capability_descriptor &caps,
                                       const std::vector<feature_requirement> &required,
                                       log_sink &log) const
{
    return gate_features(consumer, source_name, caps, required, log);
}

std::string configuration_space::generate_completion(shell which, std::string_view prog) const
{
    // Project the facade's internally held schema through the free generator. The
    // schema registry stays encapsulated -- it is never exposed; only the script
    // string crosses the facade boundary. The schema is retained across resolve
    // (run_resolve drops only the transient context's source buffers), so this
    // reads the same registered schema in either phase.
    return nucleus::generate_completion(which, m_impl->schema, prog);
}

facade_phase configuration_space::phase() const noexcept { return m_impl->phase; }

load_result configuration_space::resolve(const source_stack &stack)
{
    return m_impl->run_resolve(stack);
}

load_result configuration_space::load(const source_stack &stack)
{
    return m_impl->run_resolve(stack);
}

load_result configuration_space::load(std::vector<std::string> args)
{
    // The argv source's unknown-key recognizer bridges to the schema surface --
    // the schema is the authority over which flags exist. The recognizer captures
    // the schema by reference; it lives only for this resolve call.
    schema_registry &schema = m_impl->schema;
    argv_source argv(std::move(args));
    argv.recognize_with([&schema](const key_path &path) { return schema.recognizes(path); });

    source_stack stack;
    stack.add(argv, layer_rank::argv, "argv");
    return m_impl->run_resolve(stack);
}

load_result configuration_space::load(std::vector<std::string> paths, const document_factory &make)
{
    std::vector<std::unique_ptr<source>> docs;
    docs.reserve(paths.size());
    for(const std::string &path : paths)
    {
        std::unique_ptr<source> doc = make ? make(path) : nullptr;
        if(!doc)
            return fail(nucleus::format("no source could be built for path '{}'", path));
        docs.push_back(std::move(doc));
    }

    // Later paths overlay earlier ones: the first is the base, the rest stack
    // above it within a band clamped strictly below argv (the stable sort keeps
    // later-wins inside the band) so no config file count can outrank the CLI.
    source_stack stack;
    for(std::size_t i = 0; i < docs.size(); ++i)
        stack.add(*docs[i], document_rank(i), nucleus::format("path:{}", paths[i]));
    return m_impl->run_resolve(stack);
}

load_result configuration_space::load(std::vector<std::string> args,
                                      std::vector<std::string> paths,
                                      const document_factory &make)
{
    std::vector<std::unique_ptr<source>> docs;
    docs.reserve(paths.size());
    for(const std::string &path : paths)
    {
        std::unique_ptr<source> doc = make ? make(path) : nullptr;
        if(!doc)
            return fail(nucleus::format("no source could be built for path '{}'", path));
        docs.push_back(std::move(doc));
    }

    schema_registry &schema = m_impl->schema;
    argv_source argv(std::move(args));
    argv.recognize_with([&schema](const key_path &path) { return schema.recognizes(path); });

    // Documents layer beneath argv (which always wins): the per-document ranks are
    // clamped strictly below argv so even a long path list cannot tie or outrank
    // the command line; later files still beat earlier ones inside that band.
    source_stack stack;
    for(std::size_t i = 0; i < docs.size(); ++i)
        stack.add(*docs[i], document_rank(i), nucleus::format("path:{}", paths[i]));
    stack.add(argv, layer_rank::argv, "argv");
    return m_impl->run_resolve(stack);
}

}

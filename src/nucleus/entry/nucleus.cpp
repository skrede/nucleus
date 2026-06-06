#include "nucleus/nucleus.h"

#include "nucleus/format.h"
#include "nucleus/schema/schema_registry.h"
#include "nucleus/source/source_registry.h"
#include "nucleus/registration_policy.h"
#include "nucleus/source/argv/argv_source.h"
#include "nucleus/tokenizer/tokenizer_registry.h"
#include "nucleus/entry/resolution_context.h"

#include <memory>
#include <vector>
#include <utility>

namespace nucleus {

// The facade composition-owns the three flat sibling registries plus the host
// registration policy. This is the single place the registries are owned; they
// hold no references to one another.
class nucleus::impl
{
public:
    registration_result review(registration_kind kind, const owner_token &owner)
    {
        registration_request request{kind, owner};
        policy_verdict verdict = m_policy->review(request);
        if(!verdict.accepted())
            return fail(verdict.reason());
        return registration_ok();
    }

    // The shared resolve path: build the transient borrowing context, fold the
    // stack (expand-then-layer with provenance), freeze the immutable result, and
    // transition the state machine. Enforces the no-resolve-after-resolve rule.
    load_result run_resolve(const source_stack &stack)
    {
        if(phase != facade_phase::configurable)
            return fail(std::string(
                "load/resolve is not allowed: the facade is already resolved"));

        resolution_context ctx(schema, tokenizer, sources);
        if(auto folded = ctx.fold(stack); !folded)
            return fail(std::move(folded).error());

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
};

nucleus::nucleus() : m_impl(std::make_unique<impl>()) {}

nucleus::~nucleus() = default;

nucleus::nucleus(nucleus &&) noexcept = default;

nucleus &nucleus::operator=(nucleus &&) noexcept = default;

void nucleus::set_registration_policy(std::shared_ptr<registration_policy> policy)
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
        return fail(::nucleus::format(
            "cannot register a {} after the facade has resolved", to_string(kind)));
    return registration_ok();
}

}

registration_result nucleus::register_schema(std::string key_path, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::schema); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    m_impl->schema.add(schema_spec{std::move(key_path)}, std::move(owner));
    return registration_ok();
}

registration_result nucleus::register_tokenizer(std::string name, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::tokenizer); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(tokenizer(std::move(name), {}, {}, nullptr), std::move(owner));
    return registration_ok();
}

registration_result nucleus::register_source(std::string name, owner_token owner)
{
    if(auto guard = reject_if_resolved(m_impl->phase, registration_kind::source); !guard)
        return guard;
    if(auto verdict = m_impl->review(registration_kind::source, owner); !verdict)
        return verdict;
    m_impl->sources.add(source_spec{std::move(name)}, std::move(owner));
    return registration_ok();
}

std::size_t nucleus::schema_count() const noexcept { return m_impl->schema.size(); }

std::size_t nucleus::tokenizer_count() const noexcept { return m_impl->tokenizer.size(); }

std::size_t nucleus::source_count() const noexcept { return m_impl->sources.size(); }

facade_phase nucleus::phase() const noexcept { return m_impl->phase; }

load_result nucleus::resolve(const source_stack &stack)
{
    return m_impl->run_resolve(stack);
}

load_result nucleus::load(const source_stack &stack)
{
    return m_impl->run_resolve(stack);
}

load_result nucleus::load(std::vector<std::string> args)
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

load_result nucleus::load(std::vector<std::string> paths, const document_factory &make)
{
    std::vector<std::unique_ptr<source>> docs;
    docs.reserve(paths.size());
    for(const std::string &path : paths)
    {
        std::unique_ptr<source> doc = make ? make(path) : nullptr;
        if(!doc)
            return fail(::nucleus::format("no source could be built for path '{}'", path));
        docs.push_back(std::move(doc));
    }

    // Later paths overlay earlier ones: the first is the base, the rest stack
    // above it within a band clamped strictly below argv (the stable sort keeps
    // later-wins inside the band) so no config file count can outrank the CLI.
    source_stack stack;
    for(std::size_t i = 0; i < docs.size(); ++i)
        stack.add(*docs[i], document_rank(i), ::nucleus::format("path:{}", paths[i]));
    return m_impl->run_resolve(stack);
}

load_result nucleus::load(std::vector<std::string> args,
                          std::vector<std::string> paths,
                          const document_factory &make)
{
    std::vector<std::unique_ptr<source>> docs;
    docs.reserve(paths.size());
    for(const std::string &path : paths)
    {
        std::unique_ptr<source> doc = make ? make(path) : nullptr;
        if(!doc)
            return fail(::nucleus::format("no source could be built for path '{}'", path));
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
        stack.add(*docs[i], document_rank(i), ::nucleus::format("path:{}", paths[i]));
    stack.add(argv, layer_rank::argv, "argv");
    return m_impl->run_resolve(stack);
}

}

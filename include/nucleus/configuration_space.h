#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H

#include "nucleus/result.h"
#include "nucleus/identity.h"
#include "nucleus/log_sink.h"
#include "nucleus/registration_policy.h"

#include "nucleus/schema/schema.h"

#include "nucleus/source/source.h"
#include "nucleus/source/feature_gate.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/strain_scope.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/diagnostics/conflict_report.h"

#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <variant>
#include <functional>
#include <string_view>

namespace nucleus {

// A constructed tokenizer (defined in the internal tokenizer header). Forward
// declared so the public facade can accept an already-built tokenizer for
// install_tokenizer without the public header pulling the internal tokenizer
// machinery -- a host that injects one includes that header itself.
class tokenizer;

// The shell a completion script targets (defined in the completion header).
// Forward declared so generate_completion is reachable on the facade without the
// public header pulling the completion machinery; a host that calls it includes
// completion.h for the enumerators.
enum class shell;

// The outcome of a registration: success, or a host-supplied rejection reason
// surfaced verbatim from the registration-policy seam.
using registration_result = result<std::monostate, std::string>;

[[nodiscard]] inline registration_result registration_ok()
{
    return registration_result(std::monostate{});
}

// The two-phase lifecycle state. The facade starts configurable (register_* is
// legal); load()/resolve() transitions it to resolved (only reads are legal, and
// any further registration is an error). The transition is one-way: a resolved
// facade is done being configured.
enum class facade_phase
{
    configurable,
    resolved,
};

// The outcome of a resolve: the immutable configuration, or a reason it failed
// (a source/token error, or an attempt to resolve a facade that is already
// resolved -- the state machine enforced verbatim).
using load_result = result<configuration, std::string>;

// The configurable facade. In this phase it owns the three flat sibling
// registries (schema / tokenizer / source) as composition members and accepts
// register_schema / register_tokenizer / register_source. Every registration
// carries an opaque owner token the core stores and surfaces but never branches
// on, and is first offered to the registration-policy seam so a host can
// pre-validate or intercept it before it commits.
//
// The facade is the only owner of the registries. When cross-registry work
// begins (a later phase) it builds a transient resolution_context that BORROWS
// the registries; they never reference one another directly.
class configuration_space
{
public:
    configuration_space();
    ~configuration_space();

    configuration_space(configuration_space &&) noexcept;
    configuration_space &operator=(configuration_space &&) noexcept;

    configuration_space(const configuration_space &) = delete;
    configuration_space &operator=(const configuration_space &) = delete;

    // Installs a host registration policy. The default policy accepts every
    // registration; the core imposes no reservation or namespacing rules itself.
    void set_registration_policy(std::shared_ptr<registration_policy> policy);

    // Gates a source's capabilities against a consumer's requirements: a required
    // capability the source lacks is a loud named error; an optional one degrades
    // observably (warned through `log` and recorded). This is a HOST-CALLABLE step,
    // not auto-driven by the resolve fold, on purpose: the v0.1 schema model
    // expresses presence (required) and selector role (identity) per element but
    // does NOT yet carry per-element capability requirements, so there is nothing
    // in the schema for the fold to gate a source against. Rather than fake a
    // half-wired integration, the mechanism is exposed for a host to call with the
    // requirements it knows; richer per-element capability requirements that the
    // fold could drive itself are deferred. Reachable through the facade so a host
    // need not reach into the source headers directly.
    [[nodiscard]] gate_result gate_capabilities(std::string_view consumer,
                                                std::string_view source_name,
                                                const capability_descriptor &caps,
                                                const std::vector<feature_requirement> &required,
                                                log_sink &log) const;

    registration_result register_schema(std::string key_path, owner_token owner = {});

    // Registers a typed schema element (the real schema-as-authority model:
    // anchor::root / anchor::keyspace, required, identity). This is how a host
    // makes the schema authoritative over CONTENT -- the resolve path validates
    // the resolved keyspace against these elements, rejecting undeclared keys
    // (with a nearest-key suggestion) and missing required/identity fields.
    // Enforces referential integrity at attach time: a keyspace-anchored element
    // may only attach under an already-defined node. Subject to the same
    // state-machine and registration-policy seam as the other surfaces.
    registration_result register_element(schema_element element, owner_token owner = {});

    // Accumulates the strain selection in the configurable phase: the
    // primary-key value of the single strain to keep during resolve. When set,
    // resolve() prunes every non-matching named strain and strips the transient
    // key segment before freezing the configuration. Must be called before
    // load()/resolve(); calling after resolve is a state-machine error. A
    // resolve parameter, not a registration: it is not offered to the
    // registration-policy seam.
    registration_result select(std::string key_value);

    // Sets the composition-scope policy that governs which entries survive the
    // slice step. The default is space_open_container_closed (general entries
    // compose freely; the resolved strain's keyed entries freeze at its
    // defining layer). The policy applies whenever a strain resolves -- through
    // select() or by auto-resolving the single named strain present. Must be
    // called before load()/resolve(); calling after resolve is a state-machine
    // error. A resolve parameter, not a registration: it is not offered to the
    // registration-policy seam.
    registration_result set_strain_scope(strain_scope_policy policy);

    registration_result register_tokenizer(std::string name, owner_token owner = {});
    registration_result register_source(std::string name, owner_token owner = {});

    // Installs an already-constructed tokenizer (the generic core builtins are
    // installed automatically on construction; this is how a host injects an
    // additional one -- e.g. the opt-in HOST module via install_tokenizer(
    // make_host_tokenizer()), or a custom category). The tokenizer is moved in; a
    // later registration of the same category shadows an earlier one. Subject to
    // the same state-machine and registration-policy seam as register_tokenizer.
    registration_result install_tokenizer(tokenizer tok, owner_token owner = {});

    [[nodiscard]] std::size_t schema_count() const noexcept;
    [[nodiscard]] std::size_t tokenizer_count() const noexcept;
    [[nodiscard]] std::size_t source_count() const noexcept;

    // The key-path collisions detected during registration: each report names the
    // colliding key and every claimant (location + opaque owner token) WITHOUT
    // choosing a winner. Two registrations claiming the same key path produce one
    // report; a third extends it. Surfacing is non-adjudicating mechanism -- the
    // host queries this and decides what a collision means. Empty when none occurred.
    [[nodiscard]] std::vector<conflict_report> conflicts() const;

    // Generates a completion script for `which`, projected from the registered
    // schema and bound to the program name `prog`. This is the host-reachable
    // entry point: it delegates to the free nucleus::generate_completion over the
    // facade's internally held schema, so a host that built its schema through
    // register_element can reach completion without touching the internal schema
    // registry. A pure read of the registered schema -- it needs no resolve and is
    // callable in either phase.
    [[nodiscard]] std::string generate_completion(shell which, std::string_view prog) const;

    [[nodiscard]] facade_phase phase() const noexcept;

    // The primary resolve: fold a source set whose precedence is given EXPLICITLY
    // by the stack's per-layer ranks. Builds a transient resolution_context that
    // BORROWS the three registries, layers the stack into a building keyspace with
    // provenance recorded in the same fold, copies the values out into an
    // immutable self-owning configuration, drops the source buffers, and
    // transitions the facade to `resolved`. A second call on an already-resolved
    // facade is a state-machine error.
    [[nodiscard]] load_result resolve(const source_stack &stack);

    // Convenience overload: load an explicit source stack (alias for resolve).
    [[nodiscard]] load_result load(const source_stack &stack);

    // Convenience overload: args-only. Builds an argv source from the command line
    // at the argv precedence rank and resolves it alone, wiring the argv source's
    // unknown-key recognizer to the schema so unknown flags are caught.
    [[nodiscard]] load_result load(std::vector<std::string> args);

    // A host-supplied factory that turns a file path into a document source. The
    // core never knows a file format, so the path-based overloads delegate the
    // "path -> source" decision to the host. Returning nullptr fails the load with
    // a message naming the path.
    using document_factory = std::function<std::unique_ptr<source>(const std::string &)>;

    // Convenience overload: paths-only. Builds a document source per path through
    // the host factory and layers them at the base rank (later paths overlay
    // earlier ones), then resolves.
    [[nodiscard]] load_result load(std::vector<std::string> paths, const document_factory &make);

    // Convenience overload: both. Layers document sources (base/overlay ranks)
    // beneath the argv source (which wins), so the command line overrides files.
    [[nodiscard]] load_result load(std::vector<std::string> args,
                                   std::vector<std::string> paths,
                                   const document_factory &make);

private:
    class impl;
    std::unique_ptr<impl> m_impl;
};

}

#endif

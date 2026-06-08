#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H

#include "nucleus/expected.h"
#include "nucleus/identity.h"
#include "nucleus/log_sink.h"
#include "nucleus/registration_policy.h"

#include "nucleus/schema/schema.h"

#include "nucleus/configuration_source/feature_gate.h"
#include "nucleus/configuration_source/inherit_declaration.h"
#include "nucleus/configuration_source/configuration_source.h"

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

// A constructed tokenizer (internal header). Forward declared so install_tokenizer
// is reachable without the public header pulling the tokenizer machinery.
class tokenizer;

// The shell a completion script targets (completion header). Forward declared so
// generate_completion is reachable without pulling the completion machinery.
enum class shell;

// The outcome of a registration: success, or a host-supplied rejection reason
// surfaced verbatim from the registration-policy seam.
using registration_result = expected<std::monostate, std::string>;

[[nodiscard]] inline registration_result registration_ok()
{
    return registration_result(std::monostate{});
}

// Two-phase lifecycle state. The facade starts configurable (register_* is legal);
// load_configuration() transitions it one-way to resolved (reads only).
enum class facade_phase
{
    configurable,
    resolved,
};

// The outcome of a load: the immutable configuration, or a verbatim failure reason
// (a configuration_source/token error, or a load on an already-resolved facade).
using load_result = expected<configuration, std::string>;

// The configurable facade: sole owner of the three flat sibling registries
// (schema / tokenizer / configuration_source). Every registration carries an
// opaque owner token and is first offered to the registration-policy seam.
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

    // Sets the chain-walk inheritance policy: an admissibility callback invoked on
    // each candidate parent after pull (non-empty return rejects it) plus a depth
    // cap (default 16). Must be called before load(); after resolve is an error.
    registration_result set_inherit_policy(inherit_policy policy);

    // Host-callable capability gate: a required capability the configuration_source
    // lacks is a loud named error; an optional one degrades observably (warned and
    // recorded). Exposed for the host to call since the schema carries no per-element
    // capability requirements for the fold to drive yet.
    [[nodiscard]] gate_result gate_capabilities(std::string_view consumer,
                                                std::string_view source_name,
                                                const capability_descriptor &caps,
                                                const std::vector<feature_requirement> &required,
                                                log_sink &log) const;

    registration_result register_schema(std::string key_path, owner_token owner = {});

    // Registers a typed schema element (anchor::root / anchor::keyspace, required,
    // identity), making the schema authoritative over content: the load path
    // validates the resolved keyspace against these elements and enforces
    // referential integrity at attach time. Same state-machine/policy seam as above.
    registration_result register_element(schema_element element, owner_token owner = {});

    // Selects the single strain (by primary-key value) to keep during the load:
    // every non-matching named strain is pruned and the transient key segment
    // stripped before freezing. A load parameter, not a registration; before load().
    registration_result select(std::string key_value);

    // Sets the composition-scope policy governing which entries survive the slice
    // step (default space_open_container_closed). Applies whenever a strain resolves.
    // A load parameter, not a registration; must be called before load().
    registration_result set_strain_scope(strain_scope_policy policy);

    registration_result register_tokenizer(std::string name, owner_token owner = {});
    registration_result register_source(std::string name, owner_token owner = {});

    // Installs an already-built tokenizer (core builtins install automatically; this
    // injects an additional host-built category). Moved in; a later registration of
    // the same category shadows it. Same state-machine/policy seam as register_tokenizer.
    registration_result install_tokenizer(tokenizer tok, owner_token owner = {});

    [[nodiscard]] std::size_t schema_count() const noexcept;
    [[nodiscard]] std::size_t tokenizer_count() const noexcept;
    [[nodiscard]] std::size_t source_count() const noexcept;

    // Key-path collisions detected during registration: each report names the key
    // and every claimant (location + owner token) WITHOUT choosing a winner. The
    // host adjudicates. Empty when none occurred.
    [[nodiscard]] std::vector<conflict_report> conflicts() const;

    // Generates a completion script for `which`, projected from the registered schema
    // and bound to `prog`. A pure read of the schema; callable in either phase.
    [[nodiscard]] std::string generate_completion(shell which, std::string_view prog) const;

    [[nodiscard]] facade_phase phase() const noexcept;

    // The primary load: fold a configuration_source set whose precedence is given
    // explicitly by the stack's per-layer ranks. Borrows the three registries, layers
    // the stack into a building keyspace (recording provenance), copies values out into
    // an immutable self-owning configuration, drops the buffers, and transitions to
    // resolved. A second call on an already-resolved facade is a state-machine error.
    [[nodiscard]] load_result load_configuration(const configuration_source_stack &stack);

    // Convenience overload: an explicit stack (alias for load_configuration).
    [[nodiscard]] load_result load(const configuration_source_stack &stack);

    // Convenience overload: args-only. Builds an argv source at the argv rank and loads
    // it alone, wiring its unknown-key recognizer to the schema to catch unknown flags.
    [[nodiscard]] load_result load(std::vector<std::string> args);

    // Host-supplied factory turning a file path into a document source; the core knows
    // no file format. Returning nullptr fails the load with a message naming the path.
    using document_factory = std::function<std::unique_ptr<configuration_source>(const std::string &)>;

    // Convenience overload: paths-only. Builds a document source per path through the
    // host factory, layered at the base rank (later paths overlay earlier), then loads.
    [[nodiscard]] load_result load(std::vector<std::string> paths, const document_factory &make);

    // Convenience overload: both. Layers document sources beneath the argv source
    // (which wins), so the command line overrides files.
    [[nodiscard]] load_result load(std::vector<std::string> args,
                                   std::vector<std::string> paths,
                                   const document_factory &make);

private:
    class impl;
    std::unique_ptr<impl> m_impl;
};

}

#endif

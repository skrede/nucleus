#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H

#include "nucleus/expected.h"
#include "nucleus/identity.h"
#include "nucleus/log_sink.h"
#include "nucleus/registration_policy.h"

#include "nucleus/schema/schema.h"

#include "nucleus/configuration_source/feature_gate.h"
#include "nucleus/configuration_source/source_stack.h"
#include "nucleus/configuration_source/argv/argv_source.h"
#include "nucleus/configuration_source/inherit_declaration.h"
#include "nucleus/configuration_source/configuration_source.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/strain_scope.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/diagnostics/conflict_report.h"

#include <any>
#include <span>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <variant>
#include <optional>
#include <typeindex>
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

// The outcome of a load: the immutable configuration, or a verbatim failure reason
// (a configuration_source/token error, or a schema/conversion violation).
using load_result = expected<configuration, std::string>;

// Host-supplied factory turning a file path into a document source; the core knows
// no file format. Returning nullptr fails the load with a message naming the path.
using document_factory = std::function<std::unique_ptr<configuration_source>(const std::string &)>;

class configuration_space;

// The argv layer's per-load options: the raw argument tokens plus how an unknown
// flag is handled, an optional log sink for lenient warnings, and whether mapped
// keys are recognized against the space's schema surface (the default).
struct argv_source_options
{
    std::vector<std::string> args;
    unknown_key_policy       policy = unknown_key_policy::strict;
    log_sink                *log = nullptr;
    bool                     recognize_against_schema = true;
};

// The env layer's per-load options: host-mapped (path, text) entries.
struct env_source_options
{
    std::vector<std::pair<std::string, std::string>> entries;
};

// Everything a single load needs that is NOT part of the immutable space: which
// flat sources to layer (argv/env by value), a generic document group (paths +
// host factory), borrowed custom layers, and the per-load resolution parameters
// (strain selection, scope policy, inherit policy). Composed by value; the space
// itself carries none of this.
struct source_stack_options
{
    std::optional<argv_source_options>      argv;
    std::optional<env_source_options>       env;
    std::vector<std::string>                document_paths;
    document_factory                        make_document;
    std::vector<configuration_source_layer> custom_layers;
    std::optional<std::string>              selection;
    strain_scope_policy                     scope = strain_scope_policy::space_open_container_closed;
    inherit_policy                          inherit;
};

// Per-load resolution knobs for the new load(space, source_stack, load_options)
// entry point. Carries document expansion (paths + host factory returning source_handle)
// and the resolution parameters; the sources themselves live in the source_stack.
struct load_options
{
    std::optional<std::string>                          selection;
    strain_scope_policy                                 scope = strain_scope_policy::space_open_container_closed;
    inherit_policy                                      inherit;
    std::vector<std::string>                            document_paths;
    std::function<source_handle(const std::string &)>   make_document;
};

// The mutable, free-standing builder: sole owner of the four flat sibling
// registries (schema / tokenizer / configuration_source / converter) plus the host
// registration policy and the claim/conflict ledger. A host registers on it and
// then build() seals an immutable configuration_space. Every registration carries
// an opaque owner token and is first offered to the registration-policy seam.
class configuration_space_builder
{
public:
    configuration_space_builder();
    ~configuration_space_builder();

    configuration_space_builder(configuration_space_builder &&) noexcept;
    configuration_space_builder &operator=(configuration_space_builder &&) noexcept;

    configuration_space_builder(const configuration_space_builder &) = delete;
    configuration_space_builder &operator=(const configuration_space_builder &) = delete;

    // Installs a host registration policy. The default policy accepts every
    // registration; the core imposes no reservation or namespacing rules itself.
    registration_result set_registration_policy(std::shared_ptr<registration_policy> policy);

    registration_result register_schema(std::string key_path, owner_token owner = {});

    // Registers a typed schema element (anchor::root / anchor::keyspace, required,
    // identity), making the schema authoritative over content: the load path
    // validates the resolved keyspace against these elements and enforces
    // referential integrity at attach time. Same state-machine/policy seam as above.
    registration_result register_element(schema_element element, owner_token owner = {});

    registration_result register_tokenizer(std::string name, owner_token owner = {});
    registration_result register_source(std::string name, owner_token owner = {});

    // Installs an already-built tokenizer (core builtins install automatically; this
    // injects an additional host-built category). Moved in; a later registration of
    // the same category shadows it. Same state-machine/policy seam as register_tokenizer.
    registration_result install_tokenizer(tokenizer tok, owner_token owner = {});

    // Registers a value converter for a type, keyed by std::type_index. At resolve
    // a typed element with no per-element converter uses the converter registered
    // here for its type; a per-element converter on the element overrides it. Same
    // state-machine/policy seam as the other registrations.
    registration_result register_converter(std::type_index id,
        std::function<expected<std::any, std::string>(std::string_view)> conv,
        owner_token owner = {});

    // Convenience: register a converter for T (keyed by typeid(T)).
    template<typename T>
    registration_result register_converter(
        std::function<expected<std::any, std::string>(std::string_view)> conv,
        owner_token owner = {})
    {
        return register_converter(std::type_index(typeid(T)), std::move(conv), std::move(owner));
    }

    [[nodiscard]] std::size_t schema_count() const noexcept;
    [[nodiscard]] std::size_t tokenizer_count() const noexcept;
    [[nodiscard]] std::size_t source_count() const noexcept;
    [[nodiscard]] std::size_t converter_count() const noexcept;

    // Key-path collisions detected during registration: each report names the key
    // and every claimant (location + owner token) WITHOUT choosing a winner. The
    // host adjudicates. Empty when none occurred.
    [[nodiscard]] std::vector<conflict_report> conflicts() const;

    // Seals the builder into an immutable configuration_space. Infallible (it never
    // returns an error); it copies the four registries + policy + ledger into the
    // sealed product and marks the builder spent. After build(), every register_* /
    // install_* / set_registration_policy is a LOUD state-machine error, never a
    // silent no-op.
    [[nodiscard]] configuration_space build();

private:
    friend class configuration_space;
    class impl;
    std::unique_ptr<impl> m_impl;
};

// The sealed, immutable product of a build(): holds the four BUILT registries plus
// the policy and claim/conflict ledger. Its surface is read-only -- registration on
// a sealed space is impossible by construction. It is COPYABLE (a deep copy of the
// value-copyable registries + ledger; NO shared_ptr links two spaces) so expand()
// can clone it, and freely thread-readable: load_configuration borrows it by const
// reference and owns all mutable resolve state on its own stack.
class configuration_space
{
public:
    configuration_space();
    ~configuration_space();

    configuration_space(const configuration_space &);
    configuration_space &operator=(const configuration_space &);

    configuration_space(configuration_space &&) noexcept;
    configuration_space &operator=(configuration_space &&) noexcept;

    [[nodiscard]] std::size_t schema_count() const noexcept;
    [[nodiscard]] std::size_t tokenizer_count() const noexcept;
    [[nodiscard]] std::size_t source_count() const noexcept;
    [[nodiscard]] std::size_t converter_count() const noexcept;

    // Key-path collisions recorded during the originating builder's registrations.
    [[nodiscard]] std::vector<conflict_report> conflicts() const;

    // Generates a completion script for `which`, projected from the sealed schema
    // and bound to `prog`. A pure read of the schema.
    [[nodiscard]] std::string generate_completion(shell which, std::string_view prog) const;

    // The declared schema elements, the neutral data a format emitter projects into
    // a template. A pure read of the sealed schema; the registry stays encapsulated.
    [[nodiscard]] std::span<const schema_element> schema_elements() const;

    // Returns a NEW builder pre-populated with a DEEP COPY of this sealed space's
    // four registries + policy + claim/conflict ledger. Base and derived are fully
    // independent: building or mutating one never affects the other, and no
    // shared_ptr base pointer links them.
    [[nodiscard]] configuration_space_builder expand() const;

private:
    friend class configuration_space_builder;
    friend load_result load_configuration(const configuration_space &, const source_stack_options &);
    friend gate_result check_capabilities(const configuration_space &, const source_stack_options &);
    friend load_result   load(const configuration_space &, source_stack, const load_options &);
    friend gate_result   check_capabilities(const configuration_space &, source_stack, const load_options &);
    friend key_recognizer recognizer_of(const configuration_space &);
    class impl;
    explicit configuration_space(std::unique_ptr<impl> sealed);
    std::unique_ptr<impl> m_impl;
};

// The free load entry point: folds the per-load source stack against the sealed
// space and mints an immutable configuration. It constructs a stack-local
// resolution_context that BORROWS the space's registries by const reference and
// owns every mutable resolve buffer on its own stack frame, so multiple concurrent
// calls on one shared const configuration_space need no synchronization. It never
// mutates the space.
[[nodiscard]] load_result load_configuration(const configuration_space &space,
                                             const source_stack_options &options);

// The standalone auto-gate pre-flight: assembles the same source stack a load would
// and runs the SAME capability gate WITHOUT folding or pulling for resolution, so a
// host can validate source/schema fit ahead of time. load_configuration auto-gates
// on its own regardless of whether this was called first; the two never disagree.
[[nodiscard]] gate_result check_capabilities(const configuration_space &space,
                                             const source_stack_options &options);

// New load entry point: folds the explicitly-composed source_stack against the
// sealed space using index-as-rank precedence, optionally expanding document_paths
// from load_options through the inheritance chain walker. Concurrent-safe;
// borrows the space by const reference and owns all mutable resolve state locally.
[[nodiscard]] load_result load(const configuration_space &space,
                               source_stack stack,
                               const load_options &options = {});

// New capability pre-flight for the source_stack-based API. Reads capabilities
// only -- no pull, no fold. Consistent with load() over the same stack+options.
[[nodiscard]] gate_result check_capabilities(const configuration_space &space,
                                             source_stack stack,
                                             const load_options &options = {});

// Derives a key recognizer from the sealed space's schema surface. The returned
// closure is valid for as long as the space outlives it; it captures the space by
// reference. Used to wire argv_source schema-coupled recognition at compose time.
[[nodiscard]] key_recognizer recognizer_of(const configuration_space &space);

}

#endif

#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SPACE_H

#include "nucleus/expected.h"
#include "nucleus/identity.h"
#include "nucleus/log_sink.h"
#include "nucleus/strain_scope.h"
#include "nucleus/configuration.h"
#include "nucleus/registration_policy.h"

#include "nucleus/schema/schema.h"

#include "nucleus/configuration_source/feature_gate.h"
#include "nucleus/configuration_source/source_stack.h"
#include "nucleus/configuration_source/inherit_declaration.h"
#include "nucleus/configuration_source/configuration_source.h"
#include "nucleus/configuration_source/argv/key_recognizer.h"

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

// A constructed tokenizer (nucleus/tokenizer/tokenizer.h). Forward declared so
// install_tokenizer is reachable without this header pulling the tokenizer
// machinery into every consumer.
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

class configuration_space;

// Per-load resolution knobs for load(space, source_stack, load_options).
// Carries document expansion (paths + host factory returning source_handle)
// and the resolution parameters; the sources themselves live in the source_stack.
struct load_options
{
    std::optional<std::string>                          selection;
    strain_scope_policy                                 scope = strain_scope_policy::space_open_container_closed;
    inherit_policy                                      inherit;
    std::vector<std::string>                            document_paths;
    std::function<source_handle(const std::string &)>   make_document;
};

// The mutable, free-standing builder: sole owner of the three flat sibling
// registries (schema / tokenizer / converter) plus the host registration policy
// and the claim/conflict ledger. A host registers on it and then build() seals an
// immutable configuration_space. Every registration carries an opaque owner token
// and is first offered to the registration-policy seam.
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
    [[nodiscard]] std::size_t converter_count() const noexcept;

    // Key-path collisions detected during registration: each report names the key
    // and every claimant (location + owner token) WITHOUT choosing a winner. The
    // host adjudicates. Empty when none occurred.
    [[nodiscard]] std::vector<conflict_report> conflicts() const;

    // Seals the builder into an immutable configuration_space. Infallible (it never
    // returns an error); it copies the three registries + policy + ledger into the
    // sealed product and marks the builder spent. After build(), every register_* /
    // install_* / set_registration_policy is a LOUD state-machine error, never a
    // silent no-op.
    [[nodiscard]] configuration_space build();

private:
    friend class configuration_space;
    class impl;
    std::unique_ptr<impl> m_impl;
};

// The sealed, immutable product of a build(): holds the three BUILT registries plus
// the policy and claim/conflict ledger. Its surface is read-only -- registration on
// a sealed space is impossible by construction. It is COPYABLE (a deep copy of the
// value-copyable registries + ledger; NO shared_ptr links two spaces) so expand()
// can clone it, and freely thread-readable: load() borrows it by const reference
// and owns all mutable resolve state on its own stack.
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
    // three registries + policy + claim/conflict ledger. Base and derived are fully
    // independent: building or mutating one never affects the other, and no
    // shared_ptr base pointer links them.
    [[nodiscard]] configuration_space_builder expand() const;

private:
    friend class configuration_space_builder;
    friend load_result   load(const configuration_space &, source_stack, const load_options &);
    friend gate_result   check_capabilities(const configuration_space &, source_stack, const load_options &);
    friend key_recognizer recognizer_of(const configuration_space &);
    class impl;
    explicit configuration_space(std::unique_ptr<impl> sealed);
    std::unique_ptr<impl> m_impl;
};

// The load entry point: folds the explicitly-composed source_stack against the
// sealed space using index-as-rank precedence, optionally expanding document_paths
// from load_options through the inheritance chain walker. Concurrent-safe;
// borrows the space by const reference and owns all mutable resolve state locally.
[[nodiscard]] load_result load(const configuration_space &space,
                               source_stack stack,
                               const load_options &options = {});

// Capability pre-flight for the source_stack-based load. Reads capabilities
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

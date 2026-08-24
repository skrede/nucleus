#ifndef HPP_GUARD_NUCLEUS_CONFIG_SPACE_H
#define HPP_GUARD_NUCLEUS_CONFIG_SPACE_H

#include "nucleus/error.h"
#include "nucleus/expected.h"
#include "nucleus/identity.h"
#include "nucleus/log_sink.h"
#include "nucleus/strain_scope.h"
#include "nucleus/config.h"
#include "nucleus/registration_policy.h"

#include "nucleus/query/schema_query_context.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/cli_flag.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/config_source/feature_gate.h"
#include "nucleus/config_source/source_stack.h"
#include "nucleus/config_source/inherit_declaration.h"
#include "nucleus/config_source/config_source.h"
#include "nucleus/config_source/argv/key_recognizer.h"

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

// A constructed tree-access tokenizer (nucleus/tokenizer/tree_tokenizer.h). Forward
// declared so install_tree_tokenizer is reachable without pulling the tree-access
// machinery into every consumer.
class tree_tokenizer;

// The shell a completion script targets (completion header). Forward declared so
// generate_completion is reachable without pulling the completion machinery.
enum class shell;

// The outcome of a registration: success, or an error whose code separates a
// policy rejection (errc::rejected_registration, host reason verbatim) from a
// post-build state-machine misuse (errc::sealed_builder).
using registration_result = expected<void, error>;

inline registration_result registration_ok()
{
    return {};
}

// The outcome of load_config: the immutable config, or an error whose code
// names the pipeline stage that failed (source, inheritance, gate, layering,
// tokens, selection, schema, conversion) with the verbatim reason in message.
using load_result = expected<config, error>;

class config_space;

// Per-load resolution knobs for load_config(space, source_stack, load_options).
// Carries document expansion (paths + host factory returning source_handle)
// and the resolution parameters; the sources themselves live in the source_stack.
struct load_options
{
    std::optional<std::string>                          selection;
    strain_scope_policy                                 scope = strain_scope_policy::space_open_container_closed;
    inherit_policy                                      inherit;
    std::vector<std::string>                            document_paths;
    std::function<source_handle(const std::string &)>   make_document;
    // Maximum token substitutions in one load, spanning both token passes.
    // 0 = engine default (10000).
    std::size_t                                         expansion_budget = 0;
    // Optional host sink for load-time warnings (soft-capability degradations).
    // nullptr = no logging; degradations are still recorded on config::degradations().
    log_sink*                                           log = nullptr;
};

// The mutable, free-standing builder: sole owner of the three flat sibling
// registries (schema / tokenizer / converter) plus the host registration policy
// and the claim/conflict ledger. A host registers on it and then build() seals an
// immutable config_space. Every registration carries an opaque owner token
// and is first offered to the registration-policy seam.
class config_space_builder
{
public:
    config_space_builder();
    ~config_space_builder();

    config_space_builder(config_space_builder &&) noexcept;
    config_space_builder &operator=(config_space_builder &&) noexcept;

    config_space_builder(const config_space_builder &) = delete;
    config_space_builder &operator=(const config_space_builder &) = delete;

    // Installs a host registration policy. The default policy accepts every
    // registration; the core imposes no reservation or namespacing rules itself.
    registration_result set_registration_policy(std::shared_ptr<registration_policy> policy);

    registration_result register_schema(std::string key_path, owner_token owner = {});

    // Registers a typed schema element (anchor::root / anchor::keyspace, required,
    // identity), making the schema authoritative over content: the load_config path
    // validates the resolved keyspace against these elements and enforces
    // referential integrity at attach time. Same state-machine/policy seam as above.
    registration_result register_element(schema_element element, const owner_token& owner = {});

    // Registers a container-scoped exclusion/choice constraint group (cardinality
    // over the active members of one container instance, or a host validator).
    // Enforced on the resolved/sliced tree by load_config; a violation is loud.
    // Same state-machine/policy seam as register_element.
    registration_result register_constraint_group(constraint_group group, const owner_token& owner = {});

    // Registers an identity (key) group: a namespace pooling one identifier field
    // across the instances of several member element-types under a parent container,
    // required present and unique within a slice. The identifier is a handle (the
    // keyed-composition merge key and the keyref target), never a slice selector.
    registration_result register_identity_group(identity_group_spec group, const owner_token& owner = {});

    registration_result register_tokenizer(std::string name, owner_token owner = {});

    // Installs an already-built tokenizer (core builtins install automatically; this
    // injects an additional host-built category). Moved in; a later registration of
    // the same category shadows it. Same state-machine/policy seam as register_tokenizer.
    registration_result install_tokenizer(tokenizer tok, owner_token owner = {});

    // Installs a host-defined tree-access tokenizer for pass-2 ${category.field}
    // resolution. The resolver receives the assembled tree cursor and current leaf path
    // via tree_access; the caller must not store those references beyond the call.
    // A category colliding with a reserved name (env, string, abs, rel, scope, file,
    // dir, self) is rejected. A later registration of the same category shadows an
    // earlier one (last-registration-wins).
    registration_result install_tree_tokenizer(tree_tokenizer tok, owner_token owner = {});

    // Registers a value converter for a type, keyed by std::type_index. At resolve
    // a typed element with no per-element converter uses the converter registered
    // here for its type; a per-element converter on the element overrides it. Same
    // state-machine/policy seam as the other registrations.
    registration_result register_converter(std::type_index id,
        std::function<expected<std::any, std::string>(std::string_view)> conv,
        const owner_token& owner = {});

    // Convenience: register a converter for T (keyed by typeid(T)).
    template<typename T>
    registration_result register_converter(
        std::function<expected<std::any, std::string>(std::string_view)> conv,
        const owner_token &owner = {})
    {
        return register_converter(std::type_index(typeid(T)), std::move(conv), owner);
    }

    std::size_t schema_count() const noexcept;
    std::size_t tokenizer_count() const noexcept;
    std::size_t converter_count() const noexcept;

    // Key-path collisions detected during registration, plus any name() refused after
    // build(): each report names the key and every claimant (location + owner token)
    // WITHOUT choosing a winner. The host adjudicates. Empty when none occurred.
    std::vector<conflict_report> conflicts() const;

    // Sets the space name -- the identity each source format validates at its boundary
    // (document root element, env prefix, argv first segment). Empty = unnamed.
    config_space_builder &name(std::string space_name);

    // Seals the builder into an immutable config_space: the first build() succeeds,
    // copying the three registries + policy + ledger into the sealed product and
    // marking the builder spent. After build(), every register_* / install_* /
    // set_registration_policy is a LOUD state-machine error, a second build() reports
    // sealed_builder, and a name() refused meanwhile is recorded in conflicts() --
    // never a silent no-op.
    expected<config_space, error> build();

private:
    friend class config_space;
    class impl;
    std::unique_ptr<impl> m_impl;
};

// The sealed, immutable product of a build(): holds the three BUILT registries plus
// the policy and claim/conflict ledger. Its surface is read-only -- registration on
// a sealed space is impossible by construction. It is COPYABLE (a deep copy of the
// value-copyable registries + ledger; NO shared_ptr links two spaces) so expand()
// can clone it, and freely thread-readable: load_config() borrows it by const reference
// and owns all mutable resolve state on its own stack.
class config_space
{
public:
    config_space();
    ~config_space();

    config_space(const config_space &);
    config_space &operator=(const config_space &);

    config_space(config_space &&) noexcept;
    config_space &operator=(config_space &&) noexcept;

    std::size_t schema_count() const noexcept;
    std::size_t tokenizer_count() const noexcept;
    std::size_t converter_count() const noexcept;

    // The name set via config_space_builder::name(); empty for unnamed spaces.
    std::string_view space_name() const noexcept;

    // Key-path collisions recorded during the originating builder's registrations.
    std::vector<conflict_report> conflicts() const;

    // Generates a completion script for `which` from the sealed schema, bound to `prog`. Flags
    // render under `delimiter` and relative to `anchor`, which must match the argv_source grammar;
    // a non-empty space_name prefixes every entry, matching multispace_argv_source. A `prog` outside
    // the bare-command-token grammar (letters, digits, '.', '_', '-') is refused, emitting no script.
    expected<std::string, error> generate_completion(shell which, std::string_view prog,
                                                  const cli_delimiter &delimiter = {},
                                                  const key_path &anchor = {},
                                                  std::string_view space_name = {}) const;

    // Generates plain --help text projected from the sealed schema and bound to
    // `prog`: one line per flag with its description, allowed-values, and a
    // required marker, grouped by top-level keyspace. Flags render under
    // `delimiter` and relative to `anchor`. Only the string crosses the boundary;
    // the host owns how it is surfaced.
    std::string generate_help(std::string_view prog,
                                            const cli_delimiter &delimiter = {},
                                            const key_path &anchor = {}) const;

    // The declared schema elements, the neutral data a format emitter projects into
    // a template. A pure read of the sealed schema; the registry stays encapsulated.
    std::span<const schema_element> schema_elements() const;

    // Builds a schema_query_context snapshot for use with query(). The returned
    // context is an owned snapshot the caller may keep past this space's lifetime.
    schema_query_context query_context() const;

    // Returns a NEW builder pre-populated with a DEEP COPY of this sealed space's
    // three registries + policy + claim/conflict ledger. Base and derived are fully
    // independent: building or mutating one never affects the other, and no
    // shared_ptr base pointer links them.
    config_space_builder expand() const;

private:
    friend class config_space_builder;
    friend load_result   load_config(const config_space &, source_stack &, const load_options &);
    friend gate_result   check_capabilities(const config_space &, const source_stack &, const load_options &);
    friend key_recognizer recognizer_of(const config_space &);
    class impl;
    explicit config_space(std::unique_ptr<impl> sealed);
    std::unique_ptr<impl> m_impl;
};

// The load_config entry point: folds the explicitly-composed source_stack against the
// sealed space using index-as-rank precedence, optionally expanding document_paths
// from load_options through the inheritance chain walker. Concurrent-safe;
// borrows the space by const reference and owns all mutable resolve state locally.
// Concurrent loads may share the space but must each pass their OWN source_stack:
// the non-const source_stack& is per-load state, not shared across threads.
// The stack is BORROWED, not consumed: it stays valid afterward, so the same
// stack can pre-flight via check_capabilities() and then load_config, or load_config
// more than once (sources are pulled again; a document source reuses its cached parse).
load_result load_config(const config_space &space,
                               source_stack &stack,
                               const load_options &options = {});

// Convenience overload for inline composition: load_config(space, source_stack{...}).
// The temporary lives for the full call; nothing dangles.
load_result load_config(const config_space &space,
                               source_stack &&stack,
                               const load_options &options = {});

// Capability pre-flight for the source_stack-based load_config. Expands the
// inheritance chain -- reading and parsing every chain document -- so a missing
// chain document fails the capability check. The stack is borrowed const and stays
// intact for the load_config() that follows it. Consistent with load_config() over
// the same stack+options.
gate_result check_capabilities(const config_space &space,
                                             const source_stack &stack,
                                             const load_options &options = {});

// Derives a key recognizer from the sealed space's schema surface. The returned
// closure is valid for as long as the space outlives it; it captures the space by
// reference. Used to wire argv_source schema-coupled recognition at compose time.
key_recognizer recognizer_of(const config_space &space);

}

#endif

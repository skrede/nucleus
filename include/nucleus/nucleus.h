#ifndef HPP_GUARD_NUCLEUS_NUCLEUS_H
#define HPP_GUARD_NUCLEUS_NUCLEUS_H

#include "nucleus/result.h"
#include "nucleus/identity.h"
#include "nucleus/source/source.h"
#include "nucleus/entry/precedence.h"
#include "nucleus/entry/configuration.h"
#include "nucleus/registration_policy.h"

#include <string>
#include <memory>
#include <vector>
#include <cstddef>
#include <variant>
#include <functional>

namespace nucleus {

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
class nucleus
{
public:
    nucleus();
    ~nucleus();

    nucleus(nucleus &&) noexcept;
    nucleus &operator=(nucleus &&) noexcept;

    nucleus(const nucleus &) = delete;
    nucleus &operator=(const nucleus &) = delete;

    // Installs a host registration policy. The default policy accepts every
    // registration; the core imposes no reservation or namespacing rules itself.
    void set_registration_policy(std::shared_ptr<registration_policy> policy);

    registration_result register_schema(std::string key_path, owner_token owner = {});
    registration_result register_tokenizer(std::string name, owner_token owner = {});
    registration_result register_source(std::string name, owner_token owner = {});

    [[nodiscard]] std::size_t schema_count() const noexcept;
    [[nodiscard]] std::size_t tokenizer_count() const noexcept;
    [[nodiscard]] std::size_t source_count() const noexcept;

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

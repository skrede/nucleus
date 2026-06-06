#ifndef HPP_GUARD_NUCLEUS_NUCLEUS_H
#define HPP_GUARD_NUCLEUS_NUCLEUS_H

#include "nucleus/result.h"
#include "nucleus/identity.h"
#include "nucleus/registration_policy.h"

#include <string>
#include <memory>
#include <cstddef>
#include <variant>

namespace nucleus {

// The outcome of a registration: success, or a host-supplied rejection reason
// surfaced verbatim from the registration-policy seam.
using registration_result = result<std::monostate, std::string>;

[[nodiscard]] inline registration_result registration_ok()
{
    return registration_result(std::monostate{});
}

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

private:
    class impl;
    std::unique_ptr<impl> m_impl;
};

}

#endif

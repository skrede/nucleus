#ifndef HPP_GUARD_NUCLEUS_REGISTRATION_POLICY_H
#define HPP_GUARD_NUCLEUS_REGISTRATION_POLICY_H

#include "nucleus/identity.h"

#include <string>
#include <cstdint>
#include <string_view>

namespace nucleus {

// Identifies which registry surface a registration is bound for. The core does
// not branch on the owner token, but a host policy may legitimately apply
// different rules per surface, so the kind is surfaced to the seam.
enum class registration_kind : std::uint8_t
{
    schema,
    tokenizer,
    configuration_source,
};

[[nodiscard]] constexpr std::string_view to_string(registration_kind kind) noexcept
{
    switch(kind)
    {
        case registration_kind::schema:    return "schema";
        case registration_kind::tokenizer: return "tokenizer";
        case registration_kind::configuration_source: return "source";
    }
    return "schema";
}

// The context a registration-policy seam sees before a registration commits.
// It carries the opaque owner token (which the host may interpret -- it is the
// host's own meaning) and the surface kind, but never the core's interpretation
// of either.
struct registration_request
{
    registration_kind kind;
    owner_token owner;
};

// The verdict a policy returns. accept lets the registration commit; reject
// stops it with a host-supplied reason that the core surfaces verbatim.
class policy_verdict
{
public:
    [[nodiscard]] static policy_verdict accept() { return policy_verdict{true, {}}; }

    [[nodiscard]] static policy_verdict reject(std::string reason)
    {
        return policy_verdict{false, std::move(reason)};
    }

    [[nodiscard]] bool accepted() const noexcept { return m_accepted; }
    [[nodiscard]] const std::string &reason() const noexcept { return m_reason; }

private:
    policy_verdict(bool accepted, std::string reason)
        : m_accepted(accepted), m_reason(std::move(reason))
    {
    }

    bool m_accepted;
    std::string m_reason;
};

// The registration-policy seam: a host pre-validates / intercepts a registration
// before it commits. The default policy accepts everything (mechanism in core,
// policy in host). The core never imposes reservation or namespacing rules of
// its own beyond this hook.
class registration_policy
{
public:
    virtual ~registration_policy() = default;

    virtual policy_verdict review(const registration_request &request)
    {
        (void)request;
        return policy_verdict::accept();
    }
};

}

#endif

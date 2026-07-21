#ifndef HPP_GUARD_NUCLEUS_IDENTITY_H
#define HPP_GUARD_NUCLEUS_IDENTITY_H

#include <memory>
#include <utility>
#include <concepts>
#include <typeinfo>
#include <type_traits>

namespace nucleus {

// An opaque owner token attached to every registration. The core stores it and
// surfaces it (e.g. in conflict reports) but NEVER interprets its value: there
// is no accessor that exposes the wrapped payload for branching, only identity
// comparison. A host may wrap any equality-comparable type; two tokens are equal
// iff they wrap the same type and that type compares equal. This is the
// substrate the host uses to mean "who registered this" without the core ever
// learning the meaning.
class owner_token
{
public:
    // A token with no host payload. Anonymous tokens are never equal to each
    // other (each default-constructed token is a distinct identity) so that an
    // un-tagged registration is not silently conflated with another. The
    // distinct identity is carried by a per-instance allocation, not a host
    // value, so has_value() stays false.
    owner_token() : m_identity(std::make_shared<int>()) {}

    // The noexcept requirement is load-bearing: model::equals is a noexcept
    // override calling held == other.held, so a throwing operator== would risk
    // std::terminate. equality_comparable is added only for a legible failure.
    template <typename T>
    explicit owner_token(T value)
        requires (!std::is_same_v<std::decay_t<T>, owner_token>
                  && std::equality_comparable<std::decay_t<T>>
                  && noexcept(std::declval<const std::decay_t<T> &>()
                              == std::declval<const std::decay_t<T> &>()))
        : m_payload(std::make_shared<model<std::decay_t<T>>>(std::move(value)))
    {
    }

    bool has_value() const noexcept { return m_payload != nullptr; }

    friend bool operator==(const owner_token &a, const owner_token &b) noexcept
    {
        // Host-tagged tokens compare by wrapped type + value. Anonymous tokens
        // (no payload) compare by per-instance identity, so two un-tagged
        // registrations are never conflated. A tagged and an anonymous token
        // are never equal.
        if(a.m_payload && b.m_payload)
            return a.m_payload->equals(*b.m_payload);
        if(!a.m_payload && !b.m_payload)
            return a.m_identity == b.m_identity;
        return false;
    }

private:
    struct concept_base
    {
        concept_base() = default;
        concept_base(const concept_base &) = default;
        concept_base &operator=(const concept_base &) = default;
        concept_base(concept_base &&) = default;
        concept_base &operator=(concept_base &&) = default;
        virtual ~concept_base() = default;
        virtual const std::type_info &type() const noexcept = 0;
        virtual bool equals(const concept_base &other) const noexcept = 0;
    };

    template <typename T>
    struct model final : concept_base
    {
        explicit model(T value) : held(std::move(value)) {}

        const std::type_info &type() const noexcept override { return typeid(T); }

        bool equals(const concept_base &other) const noexcept override
        {
            if(other.type() != typeid(T))
                return false;
            return held == static_cast<const model &>(other).held;
        }

        T held;
    };

    std::shared_ptr<const concept_base> m_payload;
    std::shared_ptr<const void> m_identity;
};

}

#endif

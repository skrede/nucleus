#ifndef HPP_GUARD_NUCLEUS_REGISTRY_REGISTRATION_H
#define HPP_GUARD_NUCLEUS_REGISTRY_REGISTRATION_H

#include "nucleus/identity.h"

#include <utility>

namespace nucleus {

// A single stored registration: the host's payload (Spec) paired with the opaque
// owner token. The token is stored and surfaced but never interpreted by the
// core -- only the host attaches meaning to it.
template <typename Spec>
struct registration
{
    Spec spec;
    owner_token owner;
};

template <typename Spec>
registration<Spec> make_registration(Spec spec, owner_token owner)
{
    return registration<Spec>{std::move(spec), std::move(owner)};
}

}

#endif

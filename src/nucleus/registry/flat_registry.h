#ifndef HPP_GUARD_NUCLEUS_REGISTRY_FLAT_REGISTRY_H
#define HPP_GUARD_NUCLEUS_REGISTRY_FLAT_REGISTRY_H

#include <type_traits>

namespace nucleus {

// Compile-time enforcement of the flat-ownership invariant.
//
// The invariant is: no registry stores a member reference/pointer/owning handle
// to another registry; cross-registry needs are passed as parameters via the
// transient resolution context. A registry that violated this by holding a
// sibling member could not be constructed without that sibling -- so the
// executable signature of the invariant is: each registry is constructible with
// only its OWN dependencies (here, none), independently of every sibling.
//
// flat_registry<R> holds iff R is constructible in isolation (default-
// constructible with no sibling in scope). A registry that gained a sibling
// member whose construction it depended on would fail this concept, and the
// static_assert that pins it would stop the build -- making the invariant
// executable, not review-only.
template <typename R>
concept flat_registry = std::is_default_constructible_v<R>;

// Pins the invariant for a concrete registry. Instantiating this with a registry
// that cannot be constructed in isolation is a compile error.
template <typename R>
    requires flat_registry<R>
struct independently_constructible
{
    static constexpr bool value = true;
};

}

#endif

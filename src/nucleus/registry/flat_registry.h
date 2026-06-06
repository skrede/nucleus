#ifndef HPP_GUARD_NUCLEUS_REGISTRY_FLAT_REGISTRY_H
#define HPP_GUARD_NUCLEUS_REGISTRY_FLAT_REGISTRY_H

#include <type_traits>

namespace nucleus {

// Compile-time enforcement of the flat-ownership invariant.
//
// The invariant is: no registry stores a member reference/pointer/owning handle
// to another registry; cross-registry needs are passed as parameters via the
// transient resolution context.
//
// What this header proves AT COMPILE TIME (executable, not review-only):
//
//   1. flat_registry<R> requires R to be default-constructible. A registry that
//      held a sibling *reference* member cannot be default-constructed (a
//      reference must be bound at construction), so it fails the concept and the
//      static_assert that pins it stops the build. A registry whose only
//      constructor takes a sibling (and so has no default constructor) fails for
//      the same reason.
//
//   2. independent_of<R, Siblings...> additionally requires that R is NOT
//      constructible from a reference, pointer, or rvalue to any named sibling
//      type. This catches a primary entanglement vector that default-
//      constructibility alone misses: a registry that keeps a default
//      constructor but ALSO exposes a constructor taking a sibling by ref/ptr
//      (e.g. a factory seam that would let a sibling be captured into a member).
//      Such a registry is default-constructible -- so (1) passes it -- yet it
//      advertises a hand-off-via-construction path the flat topology forbids.
//
// What this header does NOT and CANNOT prove in C++20, and so remains
// REVIEW-ENFORCED: the mere presence of a data member of sibling type when that
// member does not break default construction. A registry holding a sibling by
// raw pointer (`sibling* m;`), an owning `std::unique_ptr<sibling>`, or a sibling
// BY VALUE is still default-constructible and is not constructible-from-sibling,
// so it passes every check above. Detecting "R has a data member of type
// sibling" requires member reflection (C++26); there is no portable, sound C++20
// predicate for it, and the fragile sizeof/triviality tricks that approximate it
// produce false results. We deliberately do not fake that coverage. Those forms
// are caught by code review against the invariant stated above, not by this
// header -- a contributor must not assume a `sibling*` member would be rejected
// here.
template <typename R>
concept flat_registry = std::is_default_constructible_v<R>;

// True iff R cannot be constructed from a reference, pointer, or rvalue to
// Sibling -- i.e. R exposes no construction-time hand-off that could capture a
// sibling. This is sound (no false positives on the legitimate registries, each
// of which is constructible only in isolation) and catches entangling
// constructors that survive the default-constructibility check.
template <typename R, typename Sibling>
concept independent_of_sibling =
    !std::is_constructible_v<R, Sibling &> &&
    !std::is_constructible_v<R, const Sibling &> &&
    !std::is_constructible_v<R, Sibling &&> &&
    !std::is_constructible_v<R, Sibling *> &&
    !std::is_constructible_v<R, const Sibling *>;

// Strengthened invariant: R is independently constructible (flat_registry) and
// is not constructible from any of the named sibling registries. Pass every
// sibling type so the entangling-constructor check covers the whole flat set.
template <typename R, typename... Siblings>
concept independent_of =
    flat_registry<R> && (independent_of_sibling<R, Siblings> && ...);

// Pins the invariant for a concrete registry against its named siblings.
// Instantiating this with a registry that cannot be constructed in isolation, or
// that exposes a constructor taking one of Siblings by ref/ptr, is a compile
// error.
template <typename R, typename... Siblings>
    requires independent_of<R, Siblings...>
struct independently_constructible
{
    static constexpr bool value = true;
};

}

#endif

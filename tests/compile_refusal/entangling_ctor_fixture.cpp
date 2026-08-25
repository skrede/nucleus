// Negative compile fixture. This file MUST fail to compile -- CTest asserts the
// failure via WILL_FAIL. It demonstrates the strengthened flat-ownership check:
// a registry can stay default-constructible yet still expose a constructor that
// takes a sibling registry by pointer, advertising a construction-time hand-off
// the flat topology forbids. independently_constructible<R, Siblings...> rejects
// it because R is constructible from a sibling pointer, even though R passes the
// plain default-constructibility concept.

#include "nucleus/registry/flat_registry.h"

#include "nucleus/schema/schema_registry.h"

#include "nucleus/tokenizer/tokenizer_registry.h"

namespace {

// Default-constructible (so flat_registry<R> alone would pass it), but it also
// exposes a constructor taking a sibling by pointer -- the forbidden vector.
class entangling_registry
{
public:
    entangling_registry() = default;
    explicit entangling_registry(nucleus::tokenizer_registry *) {}
};

// The plain concept is satisfied -- this is exactly the gap the strengthened pin
// closes, so we assert it to make the gap explicit.
static_assert(nucleus::flat_registry<entangling_registry>,
              "default-constructible, so the plain concept passes it");

// The strengthened pin is FALSE here because entangling_registry is constructible
// from a tokenizer_registry pointer, so this static_assert triggers a compile
// error -- proving the entangling-constructor vector is enforced at build time.
static_assert(nucleus::independently_constructible<
                  entangling_registry,
                  nucleus::tokenizer_registry,
                  nucleus::schema_registry>::value,
              "a registry constructible from a sibling violates flat ownership");

}

int main()
{
    return 0;
}

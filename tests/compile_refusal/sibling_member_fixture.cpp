// Negative compile fixture. This file MUST fail to compile -- CTest asserts the
// failure via WILL_FAIL. It demonstrates that the flat-ownership invariant is
// executable: a registry that stores a member reference to a sibling registry
// is no longer independently constructible, so it fails the flat_registry
// concept and the static_assert that pins it stops the build.

#include "nucleus/registry/flat_registry.h"

#include "nucleus/tokenizer/tokenizer_registry.h"

namespace {

// The cardinal sin: a registry that holds a member reference to a sibling.
// Holding the reference makes it non-default-constructible (a reference must be
// initialized), which is exactly why it cannot satisfy flat_registry.
class entangled_registry
{
public:
    explicit entangled_registry(nucleus::tokenizer_registry &sibling)
        : m_sibling(sibling)
    {
    }

private:
    nucleus::tokenizer_registry &m_sibling; // <-- the forbidden sibling member
};

// This static_assert is FALSE for entangled_registry, so it triggers a compile
// error -- proving the invariant is enforced at build time.
static_assert(nucleus::flat_registry<entangled_registry>,
              "a registry storing a sibling member violates flat ownership");

}

int main()
{
    return 0;
}

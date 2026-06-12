// Negative compile fixture. This file MUST fail to compile -- CTest asserts the
// failure via WILL_FAIL. It demonstrates that the source concept is an executable
// invariant: a struct missing pull() cannot satisfy config_source and cannot
// be placed inside a source_stack.

#include "nucleus/config_source/source_concept.h"
#include "nucleus/config_source/source_stack.h"

namespace {

// Structurally incomplete: provides capabilities() but omits the required pull().
struct missing_pull
{
    [[nodiscard]] nucleus::capability_descriptor capabilities() const { return {}; }
};

// This static_assert is FALSE for missing_pull, triggering a compile error.
static_assert(nucleus::config_source<missing_pull>,
              "a struct missing pull() must not satisfy config_source");

}

int main()
{
    // The constrained variadic ctor must also reject missing_pull.
    nucleus::source_stack stack{missing_pull{}};
    return 0;
}

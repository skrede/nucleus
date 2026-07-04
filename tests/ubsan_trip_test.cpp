// Deliberate signed-integer overflow that proves UndefinedBehaviorSanitizer is
// both armed AND non-recovering. Built and registered ONLY under a UBSan-bearing
// flavor (see tests/CMakeLists.txt), so it never runs in a normal build. The
// volatile forces the add to happen at runtime rather than folding at compile
// time. Under -fno-sanitize-recover=undefined the process aborts at the overflow;
// the sentinel below must therefore never print -- CTest asserts both halves (the
// diagnostic fired via PASS_REGULAR_EXPRESSION, the sentinel is absent via
// FAIL_REGULAR_EXPRESSION), which is what distinguishes a real abort from a
// recovered "runtime error:" that continued to exit 0.

#include <climits>
#include <cstdio>
#include <cstdlib>

int main()
{
    volatile int value = INT_MAX;
    value = value + 1;

    std::printf("unreachable-under-ubsan: %d\n", value);
    return EXIT_SUCCESS;
}

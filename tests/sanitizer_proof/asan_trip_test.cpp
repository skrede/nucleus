// Deliberately-dangling test that proves AddressSanitizer is actually wired and
// tripping. It is built and registered ONLY in a sanitizer config (see
// tests/CMakeLists.txt), so it never runs in a normal build. CTest asserts the
// process reports a heap-use-after-free, confirming ASan catches the very class
// of bug (view-into-freed-buffer) that is the project's #1 memory risk.

#include <cstdio>
#include <cstdlib>

int main()
{
    auto *buffer = new int[8];
    buffer[0] = 1234;
    delete[] buffer;

    // Read after free: AddressSanitizer must abort here with a diagnostic.
    volatile int value = buffer[0];
    std::printf("unreachable-under-asan: %d\n", value);
    return EXIT_SUCCESS;
}

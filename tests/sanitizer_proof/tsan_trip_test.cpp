// Deliberate data race: two threads write the same int with no synchronization.
// Run only under -fsanitize=thread; the harness PASSES when TSan reports the
// race, proving the sanitizer is armed (the mirror of asan_trip_test).

#include <thread>

int shared_value = 0;

int main()
{
    std::thread a([] { shared_value = 1; });
    std::thread b([] { shared_value = 2; });
    a.join();
    b.join();
    return shared_value == 0 ? 1 : 0;
}

# The concurrent-load guarantee tests spawn real threads, so the shared-const-read design is
# exercised under the sanitizer rather than asserted; that is why they link the platform's
# threading library.
find_package(Threads REQUIRED)

nucleus_add_test(concurrent_load_test LINK nucleus::runtime Threads::Threads)
nucleus_add_test(concurrent_collection_load_test LINK nucleus::runtime Threads::Threads)

nucleus_add_test(load_front_door_test LINK nucleus::runtime)

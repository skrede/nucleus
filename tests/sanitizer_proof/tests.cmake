set(nucleus_test_dir sanitizer_proof)

# Each of these deliberately commits the defect its sanitizer exists to catch -- a heap
# use-after-free, an unsynchronized cross-thread write, a signed overflow -- so a green run proves
# the instrumentation is wired and tripping rather than merely present. None is part of a normal
# run: each is built and registered only under the flavor that arms it.
if(NUCLEUS_BUILD_SANITIZER AND NUCLEUS_SANITIZER STREQUAL "address")
    add_executable(asan_trip_test ${nucleus_test_dir}/asan_trip_test.cpp)
    target_link_libraries(asan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME asan_trip_test COMMAND asan_trip_test)
    set_tests_properties(asan_trip_test PROPERTIES
        PASS_REGULAR_EXPRESSION "heap-use-after-free|AddressSanitizer")
endif()

if(NUCLEUS_BUILD_SANITIZER AND NUCLEUS_SANITIZER STREQUAL "thread")
    add_executable(tsan_trip_test ${nucleus_test_dir}/tsan_trip_test.cpp)
    target_link_libraries(tsan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME tsan_trip_test COMMAND tsan_trip_test)
    set_tests_properties(tsan_trip_test PROPERTIES
        PASS_REGULAR_EXPRESSION "data race|ThreadSanitizer")
endif()

# UBSan is armed by both the dedicated "undefined" flavor and the composed "address" flavor
# (-fsanitize=address,undefined), so this one is gated to either rather than to a single flavor.
if(NUCLEUS_BUILD_SANITIZER AND
   (NUCLEUS_SANITIZER STREQUAL "undefined" OR NUCLEUS_SANITIZER STREQUAL "address"))
    add_executable(ubsan_trip_test ${nucleus_test_dir}/ubsan_trip_test.cpp)
    target_link_libraries(ubsan_trip_test PRIVATE nucleus::nucleus)
    add_test(NAME ubsan_trip_test COMMAND ubsan_trip_test)
    # abort_on_error=0 halts via a non-zero _exit instead of raising SIGABRT (the Darwin default),
    # which CTest reports as an exception that overrides PASS_REGULAR_EXPRESSION -- neither regex
    # below would then be consulted at all. halt_on_error=1 keeps the runtime non-recovering, so the
    # process stops at the overflow and the post-overflow sentinel never prints; asserting on that
    # sentinel is what closes the hole where a recovering sanitizer lets this test pass vacuously.
    set_tests_properties(ubsan_trip_test PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=abort_on_error=0:halt_on_error=1:print_stacktrace=1"
        PASS_REGULAR_EXPRESSION "runtime error:|UndefinedBehaviorSanitizer"
        FAIL_REGULAR_EXPRESSION "unreachable-under-ubsan")
endif()

unset(nucleus_test_dir)

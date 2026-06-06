# Test framework: Catch2. Fetched via FetchContent by default so the walking
# skeleton builds anywhere; a vcpkg/find_package path can replace this later
# (scaffold default -- confirm during the project's tooling decision).
if(NUCLEUS_FETCH_TEST_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.0
    )
    FetchContent_MakeAvailable(Catch2)
else()
    find_package(Catch2 3 REQUIRED)
endif()

# Registers a Catch2 test executable built from a single source file, linked
# against the core library and Catch2's bundled main, and wired into CTest.
function(nucleus_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus Catch2::Catch2WithMain)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nucleus_add_test(version_test)
nucleus_add_test(result_test)
nucleus_add_test(format_test)
nucleus_add_test(identity_test)
nucleus_add_test(log_sink_test)

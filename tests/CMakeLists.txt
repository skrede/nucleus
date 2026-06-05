# Test framework: Catch2. Fetched via FetchContent by default so the walking
# skeleton builds anywhere; a vcpkg/find_package path can replace this later
# (scaffold default -- confirm during the project's tooling decision).
if(NUCLEUS_FETCH_TEST_DEPS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)
else()
    find_package(Catch2 3 REQUIRED)
endif()

add_executable(version_test version_test.cpp)
target_link_libraries(version_test PRIVATE nucleus::nucleus Catch2::Catch2WithMain)

add_test(NAME version_test COMMAND version_test)

# These registries must not compile, and inverting each build's result is what makes the
# flat-ownership invariant executable rather than review-only. Two distinct vectors are covered: a
# sibling REFERENCE member, which breaks default construction; and a default-constructible registry
# that still exposes a constructor taking a sibling by pointer, a construction-time hand-off the
# strengthened pin rejects even though the plain concept would pass it.
add_test(
    NAME flat_ownership_negative_compile
    COMMAND ${CMAKE_COMMAND} --build . --target sibling_member_fixture --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(flat_ownership_negative_compile PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE)

add_executable(sibling_member_fixture EXCLUDE_FROM_ALL
    ${nucleus_test_dir}/sibling_member_fixture.cpp)
target_link_libraries(sibling_member_fixture PRIVATE nucleus::nucleus)
target_include_directories(sibling_member_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/core/src
)

add_test(
    NAME flat_ownership_entangling_ctor_negative_compile
    COMMAND ${CMAKE_COMMAND} --build . --target entangling_ctor_fixture --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(flat_ownership_entangling_ctor_negative_compile PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE)

add_executable(entangling_ctor_fixture EXCLUDE_FROM_ALL
    ${nucleus_test_dir}/entangling_ctor_fixture.cpp)
target_link_libraries(entangling_ctor_fixture PRIVATE nucleus::nucleus)
target_include_directories(entangling_ctor_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/core/src
)

add_test(
    NAME source_concept_unsatisfied_compile
    COMMAND ${CMAKE_COMMAND} --build . --target source_concept_unsatisfied_fixture --config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(source_concept_unsatisfied_compile PROPERTIES WILL_FAIL TRUE RUN_SERIAL TRUE)

add_executable(source_concept_unsatisfied_fixture EXCLUDE_FROM_ALL
    ${nucleus_test_dir}/source_concept_unsatisfied_fixture.cpp)
target_link_libraries(source_concept_unsatisfied_fixture PRIVATE nucleus::nucleus)
target_include_directories(source_concept_unsatisfied_fixture PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/core/src
)

# The reintroduced-warning fixture stores an unread private field, and its compile is driven through
# a wrapper rather than a bare result inversion, which any nonzero build exit would satisfy: the
# wrapper asserts the build both fails AND fails on -Wunused-private-field, the exact diagnostic that
# once slipped through non-fatally, so an unrelated compile break cannot pass the test for the wrong
# reason. Routing it through nucleus_warnings() is the point rather than boilerplate -- it puts the
# fixture under the same centralized -Werror a developer hits locally. Clang-gated because GCC has no
# such diagnostic, so there the fixture compiles clean.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_test(
        NAME reintroduced_warning_negative_compile
        COMMAND ${CMAKE_COMMAND}
            -DNUCLEUS_BUILD_DIR=${CMAKE_BINARY_DIR}
            -DNUCLEUS_TARGET=reintroduced_warning_fixture
            -DNUCLEUS_CONFIG=$<CONFIG>
            -DNUCLEUS_EXPECT=unused-private-field
            -P ${CMAKE_SOURCE_DIR}/scripts/expect_warning_compile_failure.cmake
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(reintroduced_warning_negative_compile PROPERTIES RUN_SERIAL TRUE)

    add_executable(reintroduced_warning_fixture EXCLUDE_FROM_ALL
        ${nucleus_test_dir}/reintroduced_warning_fixture.cpp)
    nucleus_warnings(reintroduced_warning_fixture)
    target_link_libraries(reintroduced_warning_fixture PRIVATE nucleus::nucleus)
    target_include_directories(reintroduced_warning_fixture PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/core/src
    )
endif()

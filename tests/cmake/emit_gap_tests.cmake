# The ordinal-domain rejection proof drives argv, env and runtime sources, so it
# links the flat surfaces alongside the core.
nucleus_add_test(ordinal_domain_test nucleus::env nucleus::argv nucleus::runtime)

# The ordinal bound has to hold where std::size_t is 32 bits, so this probe compiles
# against the production headers alone and is run at that actual width rather than
# simulated at 64. Those headers reach the diagnostic formatting seam, so the probe
# carries a real link dependency on a standard library without std::format.
add_executable(ordinal_width_probe ${CMAKE_CURRENT_SOURCE_DIR}/ordinal_width_probe.cpp)
nucleus_warnings(ordinal_width_probe)
target_compile_features(ordinal_width_probe PRIVATE cxx_std_20)
target_link_libraries(ordinal_width_probe PRIVATE nucleus::core)

# The sub-compiles below are standalone compiler invocations that inherit nothing from
# the probe target, so its include set is read off the core target here rather than
# named: the production headers reach the generated backend header, whose root lives in
# the build tree. Bar-separated, because a CMake list would split into separate script
# arguments on the way into the driver.
set(ordinal_width_includes
    "$<JOIN:$<TARGET_PROPERTY:nucleus::core,INTERFACE_INCLUDE_DIRECTORIES>,|>")
if(NOT NUCLEUS_HAVE_STD_FORMAT)
    string(APPEND ordinal_width_includes
        "|$<JOIN:$<TARGET_PROPERTY:fmt::fmt,INTERFACE_INCLUDE_DIRECTORIES>,|>")
endif()

set(ordinal_width_driver ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_ordinal_width.cmake)
set(ordinal_width_args
    -DPROBE_EXE=$<TARGET_FILE:ordinal_width_probe>
    -DPROBE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/ordinal_width_probe.cpp
    -DINCLUDE_DIRS=${ordinal_width_includes}
    -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}
    -DNATIVE_POINTER_SIZE=${CMAKE_SIZEOF_VOID_P}
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME ordinal_32bit_test
    COMMAND ${CMAKE_COMMAND} ${ordinal_width_args} -DREQUIRE_32BIT=OFF
            -P ${ordinal_width_driver})
set_tests_properties(ordinal_32bit_test PROPERTIES SKIP_RETURN_CODE 77)

add_custom_target(ordinal_32bit_check
    COMMAND ${CMAKE_COMMAND} ${ordinal_width_args} -DREQUIRE_32BIT=ON
            -P ${ordinal_width_driver}
    DEPENDS ordinal_width_probe
    VERBATIM)

# The unavailable branch has to execute on every host, not only on one without a
# 32-bit toolchain, so both statuses are driven from fixed simulation arguments.
set(ordinal_width_simulation
    -DSIMULATE_UNAVAILABLE=ON
    -DATTEMPTED_COMMAND=simulated-32-bit-compile
    -DCOMPILER_DIAGNOSTIC=simulated-multilib-rejection
    -DREMEDIATION=install-32-bit-toolchain)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
    add_test(NAME ordinal_32bit_unavailable_normal_test
        COMMAND ${CMAKE_COMMAND} ${ordinal_width_args} ${ordinal_width_simulation}
                -DREQUIRE_32BIT=OFF -P ${ordinal_width_driver})
    add_test(NAME ordinal_32bit_unavailable_strict_test
        COMMAND ${CMAKE_COMMAND} ${ordinal_width_args} ${ordinal_width_simulation}
                -DREQUIRE_32BIT=ON -P ${ordinal_width_driver})
else()
    add_test(NAME ordinal_32bit_unavailable_normal_test
        COMMAND ordinal_width_probe --unsupported normal
                simulated-32-bit-compile simulated-multilib-rejection
                install-32-bit-toolchain)
    add_test(NAME ordinal_32bit_unavailable_strict_test
        COMMAND ordinal_width_probe --unsupported strict
                simulated-32-bit-compile simulated-multilib-rejection
                install-32-bit-toolchain)
endif()
set_tests_properties(ordinal_32bit_unavailable_normal_test
    PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(ordinal_32bit_unavailable_strict_test PROPERTIES WILL_FAIL TRUE)

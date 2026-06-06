set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(NUCLEUS_BUILD_SANITIZER AND NOT MSVC)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Coverage instrumentation, gcc/clang only and off by default -- never in a
# normal build. When on, the library and tests are compiled with gcov-compatible
# counters so a coverage report can be generated after ctest. MSVC has no gcov
# equivalent here, so the flags are guarded to the GNU/Clang front ends.
if(NUCLEUS_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage -O0 -g)
        add_link_options(--coverage)
    else()
        message(WARNING "NUCLEUS_COVERAGE is only supported on gcc/clang; ignoring.")
    endif()
endif()

# Example programs, built under -DNUCLEUS_BUILD_EXAMPLES=ON. Each is a small,
# self-contained main() focused on one concept, linked against the core library
# (and, where it needs them, the separately-linked source/tokenizer modules).
# They are demonstrations, not tests, so they are not registered with CTest.
#
# Examples are grouped by area into subdirectories; each subdirectory lists its
# own programs. The nucleus_add_example helper is defined here so every group
# reuses it without boilerplate.

# Registers an example executable linked against the core library. Additional
# link targets (e.g. nucleus::env, nucleus::argv) are passed as extra arguments.
# Relative source paths resolve against the calling subdirectory.
function(nucleus_add_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nucleus::nucleus ${ARGN})
    nucleus_warnings(${name})
endfunction()

add_subdirectory(basics)
add_subdirectory(sources)
add_subdirectory(composition)
add_subdirectory(cli)
add_subdirectory(tokens)
add_subdirectory(schema)
add_subdirectory(references)
add_subdirectory(xml)

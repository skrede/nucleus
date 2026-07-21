// A header-only module ships no compiled translation unit, so its public headers
// never enter compile_commands.json and escape clang-tidy. This TU exists solely
// to pull the runtime module's public headers into an analyzed compile.

#include "nucleus/runtime/runtime_source.h"

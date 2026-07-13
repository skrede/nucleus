// A header-only module ships no compiled translation unit, so its public headers
// never enter compile_commands.json and escape clang-tidy. This TU exists solely
// to pull the argv module's public headers into an analyzed compile.

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/cli_surface.h"
#include "nucleus/argv/argv_emitter.h"
#include "nucleus/argv/multispace_argv_source.h"

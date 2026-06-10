#ifndef HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_GENERATOR_H
#define HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_GENERATOR_H

#include "nucleus/completion/completion.h"

#include "nucleus/schema/cli_flag.h"

#include <string>
#include <string_view>

namespace nucleus {

class schema_registry;

// Generates a complete, self-contained completion script for `which`, projected
// from `schema` and bound to the program name `prog`. Flags are rendered under
// `delimiter`, which must match the argv_source grammar or completion drifts from
// the real CLI. The result is a plain string with no shell process involved --
// core stays free of any shell binary dependency. Internal: it names the
// schema_registry, so the public surface is the
// configuration_space::generate_completion member that forwards here.
[[nodiscard]] std::string generate_completion(shell which,
                                              const schema_registry &schema,
                                              std::string_view prog,
                                              const cli_delimiter &delimiter = {});

}

#endif

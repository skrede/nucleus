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
// `delimiter` and relative to `anchor`, which must match the argv_source grammar
// or completion drifts from the real CLI. The result is a plain string with no
// shell process involved -- core stays free of any shell binary dependency.
// Internal: it names the schema_registry, so the public surface is the
// config_space::generate_completion member that forwards here.
std::string generate_completion(shell which,
                                              const schema_registry &schema,
                                              std::string_view prog,
                                              const cli_delimiter &delimiter = {},
                                              const key_path &anchor = {},
                                              std::string_view space_name = {});

// Projects `schema` into plain `--help` text bound to program name `prog`: one
// line per declared flag, carrying its description, allowed-values list, and a
// required marker, grouped by the top-level keyspace (the first path segment).
// It reads the schema elements DIRECTLY -- not the completion model -- because a
// help line needs the `required` flag, which the completion model does not carry.
// Flags render under `delimiter` and relative to `anchor`, matching the argv
// grammar. The result is a plain string; the host owns how it is surfaced.
std::string generate_help(const schema_registry &schema,
                                        std::string_view prog,
                                        const cli_delimiter &delimiter = {},
                                        const key_path &anchor = {});

}

#endif

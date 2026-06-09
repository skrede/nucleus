#ifndef HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_H
#define HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_H

#include <string>
#include <string_view>

namespace nucleus {

// nucleus is a library, not a CLI: it does not ship a `completion` subcommand. It
// exposes the GENERATOR as a free function returning the script as a string, and
// the embedding host decides how to surface it (a hidden subcommand, a build-time
// file, an install hook). This keeps host vocabulary out of the core.
//
// The script is projected from the registered schema: every declared element maps
// to its canonical CLI flag (the SAME inverse mapping the argv surface uses, so
// completion can never drift from the real CLI), and an element's declared value
// set becomes that flag's completion candidates. A schema change therefore moves
// the generated completion in lockstep.

// The supported shells. bash and zsh have fundamentally different completion
// models, so each owns its own emitter behind the emission seam. fish is a future
// single-file addition through the same seam.
enum class shell
{
    bash,
    zsh
};

// The schema authority. Declared in src/; only the generator implementation (a
// .cpp) sees its definition, which keeps it an internal type while this public
// header still names it in the signature.
class schema_registry;

// Generates a complete, self-contained completion script for `shell`, projected
// from `schema` and bound to the program name `prog`. The result is a plain
// string with no shell process involved -- core stays free of any shell binary
// dependency.
[[nodiscard]] std::string generate_completion(shell which,
                                              const schema_registry &schema,
                                              std::string_view prog);

}

#endif

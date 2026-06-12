#ifndef HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_H
#define HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_H

namespace nucleus {

// nucleus is a library, not a CLI: it does not ship a `completion` subcommand.
// The host reaches the generator through config_space::generate_completion
// and decides how to surface the returned script (a hidden subcommand, a
// build-time file, an install hook). This keeps host vocabulary out of the core.
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

}

#endif

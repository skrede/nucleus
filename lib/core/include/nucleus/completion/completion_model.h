#ifndef HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_MODEL_H
#define HPP_GUARD_NUCLEUS_COMPLETION_COMPLETION_MODEL_H

#include <string>
#include <vector>

namespace nucleus {

// The shell-neutral description of one completable flag: the flag text exactly as
// the CLI surface projects it (e.g. "--logging-level"), an optional human-readable
// description, and the closed set of values it accepts (empty = unconstrained, so
// only the flag name completes). has_ordinal_wildcard marks entries generated for
// paths that cross a repeated container (D-12): the flag contains a '*' wildcard
// at the repeated-container position (e.g. "--cluster-node-*-endpoint-port").
// This carries no shell syntax whatsoever -- no quoting, no word-break handling,
// no escaping. Those concerns belong to the per-shell emitter, never to this model
// or the generator that builds it.
struct completion_option
{
    std::string flag;
    std::string description;
    std::vector<std::string> values;
    bool has_ordinal_wildcard = false;
};

// The whole completion surface for one program: the program name a shell binds the
// completion to, plus every projected option in a stable, deterministic order (so
// the generated script is reproducible byte-for-byte). The generator builds this
// purely from the schema; each shell emitter renders it into that shell's dialect.
struct completion_model
{
    std::string prog;
    std::vector<completion_option> options;
};

}

#endif

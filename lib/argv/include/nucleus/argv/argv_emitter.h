#ifndef HPP_GUARD_NUCLEUS_ARGV_ARGV_EMITTER_H
#define HPP_GUARD_NUCLEUS_ARGV_ARGV_EMITTER_H

#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/detail/flat_emitter.h"

#include <ostream>

namespace nucleus::argv {

// Projects the declared schema into flat CLI template lines: one `--KEY=` flag per
// declared LEAF path (its path joined by the delimiter, the exact flag argv_source
// parses back), blank value (template only). A constrained leaf annotates its
// allowed set as a trailing `# allowed: a|b|c`. The `--KEY=value` flavor is the
// argv vocabulary; it stays FLAT (no tree). A non-empty anchor renders keys
// relative to it and skips keys outside it -- the anchored grammar cannot
// address them.
inline void emit_template(const configuration_space &space, std::ostream &out,
                          const cli_delimiter &delimiter = {},
                          const key_path &anchor = {})
{
    detail::emit_flat_template(space, out, "--", delimiter.str(), anchor.str());
}

// Projects a resolved configuration into flat `--KEY=value` lines: one line per
// resolved value, so a repeated path emits one line per value in order. The flat
// flag contract carries no embedded newline; values are written verbatim otherwise.
inline void emit_document(const configuration &config, std::ostream &out,
                          const cli_delimiter &delimiter = {},
                          const key_path &anchor = {})
{
    detail::emit_flat_document(config, out, "--", delimiter.str(), anchor.str());
}

// The emitter modeling nucleus::config_emitter. Its only state is the flag
// grammar (delimiter + anchor), which MUST match the argv_source it round-trips
// with -- emit and parse are inverses only under one shared grammar.
struct emitter
{
    cli_delimiter delimiter;
    key_path anchor;

    void emit_template(const configuration_space &space, std::ostream &out) const
    {
        nucleus::argv::emit_template(space, out, delimiter, anchor);
    }
    void emit_document(const configuration &config, std::ostream &out) const
    {
        nucleus::argv::emit_document(config, out, delimiter, anchor);
    }
};

}

#endif

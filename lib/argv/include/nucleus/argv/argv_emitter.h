#ifndef HPP_GUARD_NUCLEUS_ARGV_ARGV_EMITTER_H
#define HPP_GUARD_NUCLEUS_ARGV_ARGV_EMITTER_H

#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/detail/flat_emitter.h"

#include <ostream>

namespace nucleus::argv {

// Projects the declared schema into flat CLI template lines: one `--KEY=` flag per
// declared LEAF path (rendered as its '/'-joined path), blank value (template only).
// A constrained leaf annotates its allowed set as a trailing `# allowed: a|b|c`. The
// `--KEY=value` flavor is the argv vocabulary; it stays FLAT (no tree).
inline void emit_template(const configuration_space &space, std::ostream &out)
{
    detail::emit_flat_template(space, out, "--");
}

// Projects a resolved configuration into flat `--KEY=value` lines: one line per
// resolved value, so a repeated path emits one line per value in order. The flat
// flag contract carries no embedded newline; values are written verbatim otherwise.
inline void emit_document(const configuration &config, std::ostream &out)
{
    detail::emit_flat_document(config, out, "--");
}

// The stateless emitter modeling nucleus::config_emitter: its members forward to the
// free functions above, so argv satisfies the output contract by type as well.
struct emitter
{
    void emit_template(const configuration_space &space, std::ostream &out) const
    {
        nucleus::argv::emit_template(space, out);
    }
    void emit_document(const configuration &config, std::ostream &out) const
    {
        nucleus::argv::emit_document(config, out);
    }
};

}

#endif

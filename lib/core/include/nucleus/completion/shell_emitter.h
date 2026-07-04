#ifndef HPP_GUARD_NUCLEUS_COMPLETION_SHELL_EMITTER_H
#define HPP_GUARD_NUCLEUS_COMPLETION_SHELL_EMITTER_H

#include "nucleus/completion/completion_model.h"

#include <string>

namespace nucleus {

// The emission seam. Each shell owns one emitter that renders the shell-neutral
// completion_model into a complete, self-contained script for that shell. Every
// per-shell concern lives behind this boundary and NOWHERE else: bash's
// COMP_WORDBREAKS splitting on `=`/`:`/`@`, zsh's `_arguments` spec grammar, and
// each shell's own quoting/escaping rules are the implementor's business. The
// generator and the model never see a backslash or a word-break -- only emitters
// do. This is what keeps a new shell (fish, later) a single-file addition rather
// than a change rippling through the core.
class shell_emitter
{
public:
    virtual ~shell_emitter() = default;

    virtual std::string emit(const completion_model &model) const = 0;

protected:
    shell_emitter() = default;
    shell_emitter(const shell_emitter &) = default;
    shell_emitter &operator=(const shell_emitter &) = default;
    shell_emitter(shell_emitter &&) = default;
    shell_emitter &operator=(shell_emitter &&) = default;
};

}

#endif

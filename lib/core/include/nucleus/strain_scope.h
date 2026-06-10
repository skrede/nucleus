#ifndef HPP_GUARD_NUCLEUS_STRAIN_SCOPE_H
#define HPP_GUARD_NUCLEUS_STRAIN_SCOPE_H

namespace nucleus {

// Composition-scope policy governing which entries survive after the slice step
// resolves one named strain. It applies rank-bounded filtering on an entry's WINNING
// rank against two first-introduction ranks recorded during the fold: Ld (the layer
// that defined the resolved strain) and Ls (the first competing strain's layer above
// Ld; unbounded if none). Flat unified-path writes (argv/env) are not instance-scoped
// and always win by plain precedence. No keyed content => no defining layer => no-op.
enum class strain_scope_policy : int
{
    // Freeze the entire keyspace at the strain's defining layer: every entry whose
    // winning rank exceeds Ld is discarded (keyed and general alike).
    file_level,

    // DEFAULT: general entries compose freely; the strain's keyed entries freeze at
    // Ld (a keyed entry whose winning rank exceeds Ld is excluded).
    space_open_container_closed,

    // The strain's keyed entries compose over [Ld, Ls) (excluded at or above Ls;
    // unbounded if no competing strain above Ld). General entries are unconstrained.
    container_open_until_next_strain,
};

}

#endif

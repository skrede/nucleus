#ifndef HPP_GUARD_NUCLEUS_STRAIN_SCOPE_H
#define HPP_GUARD_NUCLEUS_STRAIN_SCOPE_H

namespace nucleus {

// Composition-scope policy governing which entries survive after the slice step
// resolves one named strain. It applies rank-bounded filtering on an entry's WINNING
// rank against two first-introduction ranks recorded during the fold: the defining
// layer (the layer that defined the resolved strain) and the competing layer (the
// first competing strain's layer above the defining one; unbounded if none). Flat
// unified-path writes (argv/env) are not instance-scoped and always win by plain
// precedence. No keyed content => no defining layer => no-op.
enum class strain_scope_policy : int
{
    // Freeze the entire keyspace at the strain's defining layer: an entry a layer above it
    // supplied is discarded, keyed and general alike, and the value it replaced from a layer at
    // or below it stays.
    file_level,

    // DEFAULT: general entries compose freely; the strain's keyed entries freeze at the defining
    // layer: a keyed entry supplied above it is excluded, and the instance or value it displaced
    // stays.
    space_open_container_closed,

    // The strain's keyed entries compose from the defining layer up to the competing layer:
    // excluded at or above the competing layer, leaving what they displaced in place. General
    // entries are unconstrained.
    container_open_until_next_strain,
};

}

#endif

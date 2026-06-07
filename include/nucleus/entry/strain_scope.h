#ifndef HPP_GUARD_NUCLEUS_ENTRY_STRAIN_SCOPE_H
#define HPP_GUARD_NUCLEUS_ENTRY_STRAIN_SCOPE_H

namespace nucleus {

// Composition-scope policy governing which entries survive after the slice step
// resolves one named strain -- whether through an explicit selection or by
// auto-resolving the single named strain present. The policy applies
// rank-bounded filtering against ranks recorded during the fold:
//
//   Ld -- the minimum FIRST-INTRODUCTION rank among the resolved strain's keyed
//         (instance-scoped) entries: the layer that defined that strain. A later
//         overwrite of an entry does not move Ld.
//   Ls -- the minimum first-introduction rank ABOVE Ld among any competing named
//         strain's keyed entries: the first layer after the strain's defining
//         layer that introduces another strain. A competing strain introduced at
//         or below Ld does not bound the resolved strain; with no competitor
//         above Ld, Ls is unbounded.
//
// The rank bounds apply to the resolved strain's keyed (instance-scoped)
// entries; the exclusion filter tests an entry's WINNING rank, so an entry
// overwritten by a layer outside the composable window is excluded. A direct
// unified-path write (an entry already at the declared path with no key
// segment, e.g. from argv or env) is NOT instance-scoped: it composes by plain
// rank precedence and can displace the strain's value regardless of the
// container bound -- a flat command-line override always wins. The one
// exception is file_level, whose general pre-pass prunes EVERY entry above Ld.
//
// When no strain resolves at all (no keyed container content), there is no
// defining layer and scope policies are no-ops.
enum class strain_scope_policy : int
{
    // The entire keyspace is frozen at the strain's defining layer. Every entry
    // whose winning rank is greater than Ld is discarded -- the strain's keyed
    // entries and all general keyspace entries alike. Derived files (layers
    // above Ld) contribute nothing to the resolved result.
    file_level,

    // General keyspace entries compose freely from all layers (their rank is
    // unconstrained). The strain's keyed entries are frozen at Ld: entries with
    // a winning rank above Ld are excluded, so no instance-scoped change below
    // the strain's defining file takes effect. This is the DEFAULT policy when
    // the host sets nothing.
    space_open_container_closed,

    // The strain's keyed entries compose from the defining layer up to but
    // excluding Ls: entries with a winning rank in [Ld, Ls) are admitted.
    // Keyed entries with a winning rank at or above Ls are excluded. If no
    // competing strain is introduced above Ld, Ls is unbounded and all keyed
    // entries compose regardless of rank. General keyspace entries are
    // unconstrained (same as space_open_container_closed).
    container_open_until_next_strain,
};

}

#endif

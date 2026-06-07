#ifndef HPP_GUARD_NUCLEUS_ENTRY_STRAIN_SCOPE_H
#define HPP_GUARD_NUCLEUS_ENTRY_STRAIN_SCOPE_H

namespace nucleus {

// Composition-scope policy governing which entries survive after the slice step
// selects one named strain. The policy applies rank-bounded filtering against
// provenance ranks recorded during the fold:
//
//   Ld — the minimum winning provenance rank among the selected strain's
//        container-subtree entries (the defining layer of that strain).
//   Ls — the minimum winning provenance rank among any competing named strain's
//        container-subtree entries (the first layer that introduces another strain).
//
// When no strain resolves (no keyed container content, so strains map is empty
// after bucketing), Ld is undefined and scope policies are no-ops.
enum class strain_scope_policy : int
{
    // The entire keyspace is frozen at the strain's defining layer. Every entry
    // whose winning provenance rank is greater than Ld is discarded — both the
    // selected container's subtree entries and all general keyspace entries alike.
    // Derived files (layers above Ld) contribute nothing to the resolved result.
    file_level,

    // General keyspace entries compose freely from all layers (their rank is
    // unconstrained). The selected container's subtree entries are frozen at Ld:
    // entries with a winning provenance rank above Ld are excluded. Entries from
    // layers introducing other named strains do not widen the selected strain's
    // subtree. This is the DEFAULT policy when the host sets nothing.
    space_open_container_closed,

    // The container subtree composes from the defining layer up to but excluding
    // Ls: container entries with a winning provenance rank in [Ld+1, Ls-1] are
    // admitted in addition to those at or below Ld. Container entries with a
    // winning rank at or above Ls are excluded. If no competing strain exists, Ls
    // is treated as unbounded and all container entries compose regardless of rank.
    // General keyspace entries are unconstrained (same as space_open_container_closed).
    container_open_until_next_strain,
};

}

#endif

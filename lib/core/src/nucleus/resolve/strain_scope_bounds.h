#ifndef HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_SCOPE_BOUNDS_H
#define HPP_GUARD_NUCLEUS_RESOLVE_STRAIN_SCOPE_BOUNDS_H

#include "nucleus/strain_scope.h"

#include <set>
#include <cstddef>
#include <string_view>

namespace nucleus {

struct strain_scope_bounds
{
    std::string_view             identity_path;
    strain_scope_policy          policy;
    std::size_t                  Ld;
    std::size_t                  Ls;
    const std::set<std::size_t> &wide_ranks;
};

// Tested per supplying rank rather than once per strain, because a wide re-open admits only the
// content of the document that declared it.
inline bool scope_excludes(strain_scope_policy policy, std::size_t rank, std::size_t Ld,
                           std::size_t Ls, const std::set<std::size_t> &wide_ranks)
{
    if(wide_ranks.contains(rank))
        return false;
    if(policy == strain_scope_policy::container_open_until_next_strain)
        return rank >= Ls;
    return rank > Ld;
}

}

#endif

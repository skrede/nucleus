#ifndef HPP_GUARD_NUCLEUS_RESOLVE_DISPLACED_VALUES_H
#define HPP_GUARD_NUCLEUS_RESOLVE_DISPLACED_VALUES_H

#include "nucleus/resolve/resolve_types.h"

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"
#include "nucleus/keyspace/provenance.h"

#include <string>
#include <vector>
#include <cstddef>

namespace nucleus {

// A value the fold removed or overwrote, with the origin it held. The unit is what its displacer
// replaced as a whole: one instance for a repeated sweep, the path itself for a plain overwrite.
struct displaced_value
{
    key_path    path;
    value       val;
    origin      prov;
    std::string unit;
    std::size_t displaced_by;
    bool        displaced_by_chain;
};

// The fold displaces before any scope policy is known, so every displaced value is kept here
// until the slice decides whether its displacer was admissible. Recorded in fold order, which
// puts the later of two displaced versions of one path after the earlier.
class displaced_values
{
public:
    displaced_values() = default;

    void record(const key_path &path, const keyspace &building, const provenance &prov,
                const std::string &unit, const layered_handle &displacer)
    {
        const value  *held  = building.find(path);
        const origin *where = prov.of(path.str());
        if(held == nullptr || where == nullptr)
            return;
        m_displaced.push_back(displaced_value{path, held->to_owned(), *where, unit, displacer.rank,
                                              displacer.inheritance_layer.has_value()});
    }

    const std::vector<displaced_value> &all() const noexcept { return m_displaced; }

private:
    std::vector<displaced_value> m_displaced;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_RESOLVE_KEYSPACE_VALUES_H
#define HPP_GUARD_NUCLEUS_RESOLVE_KEYSPACE_VALUES_H

#include "nucleus/keyspace/value.h"
#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include <map>
#include <string>

namespace nucleus {

// Only the paths carrying a value appear; the interior paths a keyspace also
// lists are skipped.
inline std::map<std::string, std::string> owned_values(const keyspace &space)
{
    std::map<std::string, std::string> owned;
    for(const key_path &path : space.paths())
        if(const value *v = space.find(path))
            owned.emplace(path.str(), std::string(v->text()));
    return owned;
}

}

#endif

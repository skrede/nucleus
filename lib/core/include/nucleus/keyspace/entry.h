#ifndef HPP_GUARD_NUCLEUS_KEYSPACE_ENTRY_H
#define HPP_GUARD_NUCLEUS_KEYSPACE_ENTRY_H

#include "nucleus/capability.h"

#include "nucleus/keyspace/value.h"

#include <string>
#include <utility>

namespace nucleus {

// One unit a source yields through the seam: a key path, its value (view or
// owned), and the capability descriptor of the source that produced it. The
// descriptor travels with the entry so a downstream merge/validation step can
// see what the producing source could and could not represent, without holding
// a reference back to the source. The path is a `/`-separated FQN-style key
// path; a flat source emits single-segment paths.
struct keyspace_entry
{
    std::string path;
    nucleus::value value;
    capability_descriptor capabilities;
};

inline keyspace_entry make_entry(std::string path,
                                               nucleus::value value,
                                               capability_descriptor capabilities)
{
    return keyspace_entry{std::move(path), std::move(value), capabilities};
}

}

#endif

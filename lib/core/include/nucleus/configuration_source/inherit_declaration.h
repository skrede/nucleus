#ifndef HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_INHERIT_DECLARATION_H
#define HPP_GUARD_NUCLEUS_CONFIGURATION_SOURCE_INHERIT_DECLARATION_H

#include "nucleus/capability.h"

#include <string>
#include <cstddef>
#include <functional>

namespace nucleus {

// The strength of a re-open disposition: narrow obeys the active scope policy;
// wide composes regardless of scope policy.
enum class extend_strength { narrow, wide };

// A re-open declaration attached at the batch level. A derived document that
// re-opens a named instance in a base document declares the instance's container
// path and key value alongside the extend strength. Absent for flat sources that
// do not participate in inheritance.
struct extend_disposition
{
    std::string container_path; // canonical container path, e.g. "cluster/server"
    std::string key_value;      // the instance's primary-key value, e.g. "primary"
    extend_strength strength;
};

// The inheritance declaration a source returns from inheritance(). Called by
// the chain walker after pull() completes. kind::inherit_default is the default
// (equivalent to "compose with base if a base exists, otherwise no-op").
// kind::parent_path carries the declared parent file path. kind::opt_out
// explicitly truncates the chain below this source.
struct inherit_declaration
{
    enum class kind { parent_path, inherit_default, opt_out };
    kind which = kind::inherit_default;
    std::string path; // non-empty only when which == parent_path
};

// The host-injectable policy governing chain walking. admissibility is invoked
// for each candidate parent source after it is pulled; returning a non-empty
// string rejects that source and fails the load with the returned reason.
// Returning an empty string admits the source. The default (null admissibility)
// admits all sources. depth_cap is the maximum inheritance chain depth before
// the walker fails loudly; default is 16.
struct inherit_policy
{
    // Returns a non-empty rejection reason if the candidate source is not
    // admissible as a chain parent; empty string = admitted. Default admits all.
    // The callback receives the candidate's capability descriptor, which describes
    // the source's declared affordances (nesting, ordering, typed_scalars, etc.).
    std::function<std::string(capability_descriptor)> admissibility;
    std::size_t depth_cap = 16;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_VALIDATION_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_VALIDATION_H

#include "nucleus/expected.h"

#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include <set>
#include <string>
#include <vector>

namespace nucleus {

// One validation failure: the path it concerns and a human-readable reason. A
// validation run collects every violation rather than aborting on the first, so a
// host sees the whole picture in one pass.
struct schema_violation
{
    std::string path;
    std::string reason;
};

using schema_validation = expected<void, std::vector<schema_violation>>;

// The inputs every pass borrows, computed once per run: the resolved paths and
// their text forms materialized so the keyspace is parsed once rather than once
// per declared element, and the declared repeated paths the instance
// enumeration tests against.
struct validation_input
{
    const keyspace &resolved;
    std::vector<key_path> paths;
    std::vector<std::string> keys;
    const schema_registry &schema;
    std::set<std::string> repeated_declared;
};

}

#endif

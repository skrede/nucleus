#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_H

#include "nucleus/schema/anchor.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// One declared element of a schema: a named node attached under an anchor. The
// schema is the single upstream authority -- the set of registered elements
// dictates BOTH the document structure (what paths may carry values) and the CLI
// surface (what flags exist), because both are projections of the same elements.
//
// Two constraints live here and are deliberately KEPT DISTINCT:
//
//   required -- the element must carry a value once resolved. A presence
//               constraint: "this field has to be set."
//
//   identity -- the element is the selector / primary key of its parent node:
//               the field whose value names WHICH record/node an overlay applies
//               to (powers file-overlay matching and multi-node documents). A
//               role constraint: "this field identifies the node."
//
// These are different axes. A field can be required without being an identity
// (a mandatory setting), an identity without being required (an optional
// selector that defaults), both, or neither. The model never collapses one into
// the other; the enforcer checks them independently.
struct schema_element
{
    // The element's own name (the leaf segment it contributes to the keyspace).
    std::string name;
    // Where it attaches. root() introduces `name` as a top-level keyspace;
    // keyspace(path) attaches `name` under an already-defined node at `path`.
    anchor at = anchor::root();
    // Presence constraint: must carry a value at resolve.
    bool required = false;
    // Role constraint: this element is its parent node's identity/selector.
    bool identity = false;

    // The closed set of values this element accepts, if it is constrained. An
    // empty vector (the default) means unconstrained -- any value is admissible.
    // A non-empty vector is a value constraint: a resolved value outside the set
    // is a validation error, and the set is the candidate list the projected
    // shell completion offers for `--flag=<value>`. Kept a plain string vector so
    // the core stays domain-neutral and free of any enum-reflection dependency.
    std::vector<std::string> allowed_values;

    // The full keyspace path this element declares: the anchor's path extended by
    // the element's name. A root element declares a single-segment top-level
    // path; a nested element declares the anchor path + name.
    [[nodiscard]] key_path declared_path() const
    {
        return at.under().child(name);
    }
};

// Fluent helpers so a host declares schema elements readably without naming the
// boolean axes positionally.
[[nodiscard]] inline schema_element element(std::string name, anchor at)
{
    return schema_element{std::move(name), std::move(at), false, false, {}};
}

[[nodiscard]] inline schema_element required_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.required = true;
    return e;
}

[[nodiscard]] inline schema_element identity_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.identity = true;
    return e;
}

// An element whose value is constrained to a closed set. The values are both the
// validation allow-list (a resolved value outside the set is rejected) and the
// candidate list the projected shell completion offers for this flag.
[[nodiscard]] inline schema_element enum_element(std::string name, anchor at,
                                                 std::vector<std::string> values)
{
    schema_element e = element(std::move(name), std::move(at));
    e.allowed_values = std::move(values);
    return e;
}

}

#endif

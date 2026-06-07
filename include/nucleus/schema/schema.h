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
// Constraints live here and are deliberately KEPT DISTINCT on separate axes:
//
//   required -- the element must carry a value once resolved. A presence
//               constraint: "this field has to be set."
//
//   identity -- the element is the PRIMARY KEY of its parent container: the
//               single field whose value names WHICH instance of a repeatable
//               container an overlay or a slice applies to. There is at MOST one
//               identity per container (enforced at attach). It powers slicing
//               (select the instance whose key equals a value, prune the rest)
//               and overlay matching across multi-instance documents. A role
//               constraint: "this field selects the instance." Primary key and
//               identity are the same concept; `identity` is the in-code spelling.
//
//   unique   -- the element's value must be DISTINCT across sibling instances of
//               its parent container. Unlike identity there may be MANY unique
//               fields, and a unique field carries no selector role and is never
//               consumed by a slice. A primary key is implicitly unique (it must
//               distinguish instances); an ordinary unique field is just "no two
//               siblings may share this value."
//
// These are independent axes. A field can be required without being a key, a key
// without being required, unique without being a key, or any combination. The
// model never collapses one into another; the enforcer checks each separately.
struct schema_element
{
    // The element's own name (the leaf segment it contributes to the keyspace).
    std::string name;
    // Where it attaches. root() introduces `name` as a top-level keyspace;
    // keyspace(path) attaches `name` under an already-defined node at `path`.
    anchor at = anchor::root();
    // Presence constraint: must carry a value at resolve.
    bool required = false;
    // Role constraint: this element is its parent container's primary key. At
    // most one per container; implies value-uniqueness across instances.
    bool identity = false;
    // Constraint: this element's value is unique across sibling container
    // instances. Many allowed; carries no selector role. (A primary key is
    // implicitly unique whether or not this is also set.)
    bool unique = false;
    // Collection mode: this element keeps ALL N values of the same-named field
    // as an ordered collection. Distinct from keyed containers (instances) and
    // template merging. Leaf fields only.
    bool repeated = false;

    // The closed set of values this element accepts, if it is constrained. An
    // empty vector (the default) means unconstrained -- any value is admissible.
    // A non-empty vector is a value constraint: a resolved value outside the set
    // is a validation error, and the set is the candidate list the projected
    // shell completion offers for `--flag=<value>`. Kept a plain string vector so
    // the core stays domain-neutral and free of any enum-reflection dependency.
    std::vector<std::string> allowed_values;

    // True when this element is its parent container's primary key OR is declared
    // unique -- i.e. its value must be distinct across sibling instances. A
    // primary key is uniqueness-bearing even without the `unique` flag set.
    [[nodiscard]] bool enforces_uniqueness() const noexcept
    {
        return identity || unique;
    }

    // The full keyspace path this element declares: the anchor's path extended by
    // the element's name. A root element declares a single-segment top-level
    // path; a nested element declares the anchor path + name.
    [[nodiscard]] key_path declared_path() const
    {
        return at.under().child(name);
    }

    // The container this element is a field of: its parent path. For a primary
    // key or a unique field this is the repeatable container whose instances the
    // field distinguishes. Empty for a root-anchored element.
    [[nodiscard]] key_path container() const
    {
        return at.under();
    }
};

// Fluent helpers so a host declares schema elements readably without naming the
// boolean axes positionally.
[[nodiscard]] inline schema_element element(std::string name, anchor at)
{
    schema_element e;
    e.name = std::move(name);
    e.at = std::move(at);
    return e;
}

[[nodiscard]] inline schema_element required_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.required = true;
    return e;
}

// The primary key of its parent container: the one field a slice selects on.
// Exactly one per configuration space -- it is the single slice selector for
// the whole schema hierarchy. A key VALUE must not shadow a declared sibling
// element's name (an instance literally named like a leaf can never be
// bucketed; resolve rejects the collision loudly). `identity_element` is the
// established spelling; `primary_key_element` is an alias for hosts that think
// in primary-key terms.
[[nodiscard]] inline schema_element identity_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.identity = true;
    return e;
}

[[nodiscard]] inline schema_element primary_key_element(std::string name, anchor at)
{
    return identity_element(std::move(name), std::move(at));
}

// A field whose value must be distinct across sibling instances of its parent
// container, without being the selector. Many such fields may exist per container.
[[nodiscard]] inline schema_element unique_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.unique = true;
    return e;
}

// A leaf field that keeps ALL N occurrences of a same-named entry as an ordered
// collection. The fold appends within a source layer and replaces across layers.
// get() returns the last value; get_all() returns the full collection.
[[nodiscard]] inline schema_element repeated_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.repeated = true;
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

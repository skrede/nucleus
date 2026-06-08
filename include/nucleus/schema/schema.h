#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_H

#include "nucleus/expected.h"

#include "nucleus/schema/anchor.h"

#include "nucleus/keyspace/key_path.h"

#include <any>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <typeindex>
#include <functional>

namespace nucleus {

// One declared element of a schema: a named node under an anchor. The set of
// elements is the single upstream authority dictating both the document structure
// and the CLI surface (both are projections of the same elements). Its constraints
// are independent axes the enforcer checks separately: required (must carry a
// value), identity (the parent container's single primary key / slice selector,
// at most one per container), and unique (value distinct across sibling instances,
// many allowed, no selector role; a primary key is implicitly unique).
struct schema_element
{
    // The element's own name (the leaf segment it contributes to the keyspace).
    std::string name;
    // Where it attaches. root() introduces `name` as a top-level keyspace;
    // keyspace(path) attaches `name` under an already-defined node at `path`.
    anchor at = anchor::root();
    // Presence constraint: must carry a value at load.
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

    // Optional type-erased converter: converts a resolved string_view to a typed
    // value at the load boundary. Null (default) means untyped (string-only via
    // get()). Set with type_identity by the typed_element factory. Converters must
    // not throw; return unexpected() for any conversion error.
    std::function<expected<std::any, std::string>(std::string_view)> converter;

    // The std::type_index of the type T that the converter produces. Present iff
    // converter is set; used by get_as<T> to enforce outright type equality.
    std::optional<std::type_index> type_identity;

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

// The primary key of its parent container: the single slice selector for the whole
// schema hierarchy (exactly one per configuration space). A key VALUE must not shadow
// a declared sibling element's name (the load rejects the collision loudly).
// `primary_key_element` is an alias for hosts that think in primary-key terms.
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

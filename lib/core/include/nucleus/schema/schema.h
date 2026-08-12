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

// How a repeated/identified collection combines across config layers. The mode
// changes only the COMBINE OP within the existing source-stack precedence order; it
// never changes layer ordering. unite and replace_by_key require an identity group
// (its field is the merge key). None of the modes field-merge -- a resolved element
// is always wholly from one layer -- so deep field-patch (modify_by_key) is deferred.
enum class merge_mode
{
    // Default: a higher layer replaces, whole, each instance it supplies, and leaves
    // the lower layer's instances it does not address in place.
    wholesale_replace,
    // The union / strict-additive mode: layers union; a duplicate identifier across
    // layers is a loud error (additions only, never an accidental override).
    unite,
    // Layers union; a matching identifier replaces that whole element with the higher
    // layer's version (never a per-field merge).
    replace_by_key,
};

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
    // Collection mode: N sibling instances each occupy a distinct ordinal slot.
    // Legal on any element -- leaf (scalar instances) or container (structured instances).
    bool repeated = false;

    // Cross-layer combination mode for a repeated/identified collection. The default
    // replaces per ordinal instance; unite/replace_by_key key on an identity group's field.
    merge_mode merge = merge_mode::wholesale_replace;

    // When non-empty, this leaf is a keyref: its value names a target in the identity
    // group named here. A value matching no identifier in that namespace (within the
    // slice) is a loud dangling-reference error; the value dereferences to its target
    // node via follow_keyref(). Empty (default) means this is an ordinary leaf.
    std::string keyref_into;

    // The closed set of values this element accepts, if it is constrained. An
    // empty vector (the default) means unconstrained -- any value is admissible.
    // A non-empty vector is a value constraint: a resolved value outside the set
    // is a validation error, and the set is the candidate list the projected
    // shell completion offers for `--flag=<value>`. Kept a plain string vector so
    // the core stays domain-neutral and free of any enum-reflection dependency.
    std::vector<std::string> allowed_values;

    // A short, human-readable description of this element. It is the single source
    // the shell completions and the projected --help text both read; empty (the
    // default) means the flag carries no help text.
    std::string description;

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
    bool enforces_uniqueness() const noexcept
    {
        return identity || unique;
    }

    // The full keyspace path this element declares: the anchor's path extended by
    // the element's name. A root element declares a single-segment top-level
    // path; a nested element declares the anchor path + name.
    key_path declared_path() const
    {
        return at.under().child(name);
    }

    // The container this element is a field of: its parent path. For a primary
    // key or a unique field this is the repeatable container whose instances the
    // field distinguishes. Empty for a root-anchored element.
    key_path container() const
    {
        return at.under();
    }
};

// Fluent helpers so a host declares schema elements readably without naming the
// boolean axes positionally.
inline schema_element element(std::string name, anchor at)
{
    schema_element e;
    e.name = std::move(name);
    e.at = std::move(at);
    return e;
}

inline schema_element required_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.required = true;
    return e;
}

// The primary key of its parent container: the single slice selector for the whole
// schema hierarchy (exactly one per config space). A key VALUE must not shadow
// a declared sibling element's name (the load rejects the collision loudly).
// `primary_key_element` is an alias for hosts that think in primary-key terms.
inline schema_element identity_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.identity = true;
    return e;
}

inline schema_element primary_key_element(std::string name, anchor at)
{
    return identity_element(std::move(name), std::move(at));
}

// A field whose value must be distinct across sibling instances of its parent
// container, without being the selector. Many such fields may exist per container.
inline schema_element unique_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.unique = true;
    return e;
}

// An element that keeps N sibling instances in ordinal (document) order.
// For a leaf, instances are scalars; for a container, instances are indexed subtrees.
inline schema_element repeated_element(std::string name, anchor at)
{
    schema_element e = element(std::move(name), std::move(at));
    e.repeated = true;
    return e;
}

// Sets the cross-layer combination mode on a repeated element. unite/replace_by_key
// require an identity group whose field is the merge key. Reads as
// merging(repeated_element("output", at), merge_mode::unite).
inline schema_element merging(schema_element e, merge_mode mode)
{
    e.merge = mode;
    return e;
}

// A field typed as a reference into the named identity namespace (an identity group).
// Its value names a target; a value matching no identifier is a loud dangling-reference
// error, and the value dereferences to its target node via follow_keyref(). The target
// namespace is named explicitly via `into`, never inferred. The identity group must be
// registered before the keyref that references it.
inline schema_element keyref_element(std::string name, anchor at, std::string into)
{
    schema_element e = element(std::move(name), std::move(at));
    e.keyref_into = std::move(into);
    return e;
}

// An element whose value is constrained to a closed set. The values are both the
// validation allow-list (a resolved value outside the set is rejected) and the
// candidate list the projected shell completion offers for this flag.
inline schema_element enum_element(std::string name, anchor at,
                                                 std::vector<std::string> values)
{
    schema_element e = element(std::move(name), std::move(at));
    e.allowed_values = std::move(values);
    return e;
}

// Attaches a human-readable description to an element. Reads as
// described(enum_element("level", at, {...}), "set the logging level"); the
// description feeds both the shell completions and the projected --help text.
inline schema_element described(schema_element e, std::string text)
{
    e.description = std::move(text);
    return e;
}

}

#endif

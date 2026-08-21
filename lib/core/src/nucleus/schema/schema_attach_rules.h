#ifndef HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ATTACH_RULES_H
#define HPP_GUARD_NUCLEUS_SCHEMA_SCHEMA_ATTACH_RULES_H

#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/schema_defined_nodes.h"

#include "nucleus/format.h"
#include "nucleus/expected.h"

#include "nucleus/keyspace/key_path.h"

#include <span>
#include <cctype>
#include <string>
#include <algorithm>

namespace nucleus {

// The outcome of attaching a schema element: success, or a referential-integrity
// rejection naming the undefined keyspace it tried to attach under.
using schema_attach_result = expected<void, std::string>;

inline schema_attach_result check_anchor_wellformed(const schema_element &el)
{
    if(!el.at.is_invalid())
        return {};
    return unexpected(nucleus::format(
        "schema element '{}' has a malformed keyspace anchor '{}'",
        el.name, el.at.invalid_path()));
}

inline schema_attach_result check_anchor_defined(const schema_element &el,
                                                 const schema_defined_nodes &defined)
{
    if(el.at.is_root())
        return {};
    const key_path &under = el.at.under();
    if(defined.contains_node(under))
        return {};
    return unexpected(nucleus::format(
        "schema element '{}' cannot attach under undefined keyspace '{}'",
        el.name, under.str()));
}

// A config space has exactly ONE primary key: it is the single slice selector for
// the whole schema hierarchy (many strains shipped, one resolved through the key).
// A second identity element ANYWHERE -- same container or not -- would make the
// selector ambiguous, so it is rejected at attach and the schema can never
// express it.
inline schema_attach_result check_single_primary_key(const schema_element &el,
                                                     std::span<const schema_element> declared)
{
    if(!el.identity)
        return {};
    const auto existing = std::ranges::find_if(
        declared, [](const schema_element &e) { return e.identity; });
    if(existing == declared.end())
        return {};
    return unexpected(nucleus::format(
        "schema element '{}' cannot be a primary key: '{}' is "
        "already the config space's primary key, and a "
        "space has exactly one",
        el.name, existing->declared_path().str()));
}

inline schema_attach_result check_not_repeated_primary_key(const schema_element &el)
{
    if(!el.repeated || !el.identity)
        return {};
    return unexpected(nucleus::format(
        "schema element '{}' cannot be both repeated and a primary key: "
        "a primary key must be a unique scalar, not a collection",
        el.name));
}

inline schema_attach_result check_not_repeated_unique(const schema_element &el)
{
    if(!el.repeated || !el.unique)
        return {};
    return unexpected(nucleus::format(
        "schema element '{}' cannot be both repeated and unique: "
        "uniqueness requires a single comparable value, not a collection",
        el.name));
}

// Element names must not start with a digit so CLI flag text is unambiguously
// invertible back to a schema path (numeric leading chars would collide with
// ordinal index notation in the CLI bijection).
inline schema_attach_result check_name_not_digit_led(const schema_element &el)
{
    if(el.name.empty() || !std::isdigit(static_cast<unsigned char>(el.name.front())))
        return {};
    return unexpected(nucleus::format(
        "schema element '{}' has a digit-led name: element names must not "
        "start with a digit (CLI flag disambiguation requires this)",
        el.name));
}

// A primary key under ANY repeated ancestor is ambiguous -- each ordinal instance
// would need its own selector, which v1 does not support. Walk the full ancestor
// chain of the container; reject if any ancestor is a declared repeated element.
inline schema_attach_result check_no_repeated_ancestor(const schema_element &el,
                                                       std::span<const schema_element> declared)
{
    if(!el.identity)
        return {};
    for(key_path a = el.container(); !a.empty(); a = a.parent())
    {
        const auto ancestor = std::ranges::find_if(
            declared, [&](const schema_element &e) {
                return e.repeated && e.declared_path() == a;
            });
        if(ancestor != declared.end())
            return unexpected(nucleus::format(
                "schema element '{}' is a primary key under repeated "
                "ancestor '{}': keyed selection has no clean per-instance "
                "meaning inside a repeated container (v1 restriction)",
                el.name, a.str()));
    }
    return {};
}

// A keyref's `into=` must name an already-registered identity group (declare the
// namespace before the keyref that references it), mirroring the
// declare-before-reference rule the keyspace anchor enforces.
inline schema_attach_result
check_keyref_target_registered(const schema_element &el,
                               std::span<const identity_group_spec> groups)
{
    if(el.keyref_into.empty())
        return {};
    const bool known = std::ranges::any_of(
        groups, [&](const identity_group_spec &g) { return g.name == el.keyref_into; });
    if(known)
        return {};
    return unexpected(nucleus::format(
        "keyref '{}' references identity namespace '{}', which is not a "
        "registered identity group",
        el.name, el.keyref_into));
}

// An element path may be declared once. Re-declaring it would let a later element
// silently override the first's role by declaration order (a pkey smuggled under a
// repeated container, a second typed element dropped at the typed store).
// Membership is over declared ELEMENTS by exact path -- prefix nodes stay
// admissible so children still attach under a declared container, and a
// path-tagged registration is a separate surface adjudicated as a conflict rather
// than rejected here.
inline schema_attach_result check_path_not_redeclared(const schema_element &el,
                                                      std::span<const schema_element> declared)
{
    const bool duplicate = std::ranges::any_of(
        declared, [&](const schema_element &e) {
            return e.declared_path() == el.declared_path();
        });
    if(!duplicate)
        return {};
    return unexpected(nucleus::format(
        "schema element '{}' re-declares already-declared path '{}'",
        el.name, el.declared_path().str()));
}

}

#endif

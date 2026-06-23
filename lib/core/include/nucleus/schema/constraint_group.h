#ifndef HPP_GUARD_NUCLEUS_SCHEMA_CONSTRAINT_GROUP_H
#define HPP_GUARD_NUCLEUS_SCHEMA_CONSTRAINT_GROUP_H

#include "nucleus/expected.h"
#include "nucleus/config_node.h"

#include "nucleus/schema/anchor.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <optional>
#include <functional>

namespace nucleus {

// One member of a container-scoped constraint group. A bare member is active iff
// present; with active_value set it is active iff its resolved value equals that
// value (bools/enums); a non-empty bundle is an all_of(...) co-required set that is
// active iff every named field is present (and a co-requirement violation when only
// some are).
struct group_member
{
    std::string                name;
    std::optional<std::string> active_value;
    std::vector<std::string>   bundle;
};

// Activation tag so member("flag", when_value("true")) reads as a named axis.
struct when_value
{
    std::string value;
    explicit when_value(std::string v) : value(std::move(v)) {}
};

inline group_member member(std::string name)
{
    return group_member{std::move(name), std::nullopt, {}};
}

inline group_member member(std::string name, when_value activation)
{
    return group_member{std::move(name), std::move(activation.value), {}};
}

// A co-required bundle: every named field present, or none. One choice option or one
// exclusion member; counts as a single active option toward the group's cardinality.
inline group_member all_of(std::vector<std::string> names)
{
    return group_member{std::string{}, std::nullopt, std::move(names)};
}

// The cardinality clause over the count of active members in one container instance.
enum class group_bound { at_most, exactly, at_least };

// A container-anchored constraint over a member set, carrying a named diagnostic.
// Either a cardinality clause over its members, or a host validator run over
// the resolved container (the escape hatch for the rare case cardinality cannot cover
// -- there is deliberately NO predicate/boolean-constraint grammar).
struct constraint_group
{
    std::string                                                      name;
    anchor                                                           at = anchor::root();
    std::vector<group_member>                                        members;
    group_bound                                                      bound = group_bound::at_most;
    std::size_t                                                      count = 1;
    std::function<expected<void, std::string>(const config_node &)>  validator;

    key_path container() const { return at.under(); }
};

// Fluent: exclusion_group("name", anchor::keyspace(container)).members({...}).at_most(1).
// The cardinality verb is the terminal -- it returns the finished constraint_group, so
// register_constraint_group(exclusion_group(...).at_most(1)) needs no conversion operator.
// Cardinality is over the active members of ONE container instance.
class exclusion_group
{
public:
    exclusion_group(std::string name, anchor at)
    {
        m_group.name = std::move(name);
        m_group.at = std::move(at);
    }

    exclusion_group &members(std::vector<std::string> names)
    {
        for(std::string &n : names)
            m_group.members.push_back(nucleus::member(std::move(n)));
        return *this;
    }

    exclusion_group &member(std::string name)
    {
        m_group.members.push_back(nucleus::member(std::move(name)));
        return *this;
    }

    exclusion_group &member(std::string name, when_value activation)
    {
        m_group.members.push_back(nucleus::member(std::move(name), std::move(activation)));
        return *this;
    }

    exclusion_group &member(group_member m)
    {
        m_group.members.push_back(std::move(m));
        return *this;
    }

    constraint_group at_most(std::size_t n)  { m_group.bound = group_bound::at_most;  m_group.count = n; return std::move(m_group); }
    constraint_group exactly(std::size_t n)  { m_group.bound = group_bound::exactly;  m_group.count = n; return std::move(m_group); }
    constraint_group at_least(std::size_t n) { m_group.bound = group_bound::at_least;  m_group.count = n; return std::move(m_group); }

private:
    constraint_group m_group;
};

// Fluent: choice("name", anchor).option(all_of({...})).option(all_of({...})).exactly(1).
// Selects exactly N co-required bundles (mode selection); exactly(...) is the terminal.
class choice
{
public:
    choice(std::string name, anchor at)
    {
        m_group.name = std::move(name);
        m_group.at = std::move(at);
        m_group.bound = group_bound::exactly;
        m_group.count = 1;
    }

    choice &option(group_member bundle)
    {
        m_group.members.push_back(std::move(bundle));
        return *this;
    }

    constraint_group exactly(std::size_t n) { m_group.count = n; return std::move(m_group); }

private:
    constraint_group m_group;
};

// Sugar: at most one of the named members is active (the canonical mutual exclusion).
inline constraint_group mutually_exclusive(std::string name, anchor at,
                                           std::vector<std::string> names)
{
    constraint_group g;
    g.name = std::move(name);
    g.at = std::move(at);
    for(std::string &n : names)
        g.members.push_back(member(std::move(n)));
    g.bound = group_bound::at_most;
    g.count = 1;
    return g;
}

// A host validator run over each resolved container instance.
inline constraint_group
validate_group(std::string name, anchor at,
               std::function<expected<void, std::string>(const config_node &)> validator)
{
    constraint_group g;
    g.name = std::move(name);
    g.at = std::move(at);
    g.validator = std::move(validator);
    return g;
}

inline constraint_group
validate_group(anchor at,
               std::function<expected<void, std::string>(const config_node &)> validator)
{
    return validate_group("validate_group", std::move(at), std::move(validator));
}

}

#endif

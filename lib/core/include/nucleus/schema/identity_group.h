#ifndef HPP_GUARD_NUCLEUS_SCHEMA_IDENTITY_GROUP_H
#define HPP_GUARD_NUCLEUS_SCHEMA_IDENTITY_GROUP_H

#include "nucleus/schema/anchor.h"

#include "nucleus/keyspace/key_path.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// A named namespace that pools the `field` identifier across the instances of several
// member element-TYPES under one parent container, requiring it present and unique
// within a slice. The identifier is a HANDLE that survives as readable data -- the
// merge key for keyed composition and the keyref target -- NOT a second primary key:
// it does not slice and is never consumed.
struct identity_group_spec
{
    std::string              name;
    anchor                   at = anchor::root();
    std::vector<std::string> members;
    std::string              field;

    key_path container() const { return at.under(); }
};

// Fluent: identity_group("namespace", anchor::keyspace(parent)).members({...}).field("name").
// field(...) is the terminal -- it returns the finished spec, so register_identity_group(...)
// needs no conversion operator.
class identity_group
{
public:
    identity_group(std::string name, anchor at)
    {
        m_spec.name = std::move(name);
        m_spec.at = std::move(at);
    }

    identity_group &members(std::vector<std::string> names)
    {
        m_spec.members = std::move(names);
        return *this;
    }

    identity_group_spec field(std::string field_name)
    {
        m_spec.field = std::move(field_name);
        return std::move(m_spec);
    }

private:
    identity_group_spec m_spec;
};

}

#endif

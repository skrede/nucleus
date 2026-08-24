#ifndef HPP_GUARD_NUCLEUS_RESOLVE_PKEY_TREE_TOKENIZER_H
#define HPP_GUARD_NUCLEUS_RESOLVE_PKEY_TREE_TOKENIZER_H

#include "nucleus/format.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/schema/schema.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include <string>
#include <utility>

namespace nucleus {

inline token_result resolve_pkey_field(const key_path &pkey_container,
                                       const std::string &identity_field,
                                       const tree_access &access)
{
    // Verify an instance is selected by checking the identity leaf.
    key_path const identity_path = pkey_container.child(identity_field);
    if(access.building.find(identity_path) == nullptr)
        return unexpected(resolve_error(resolve_errc::missing_field,
            nucleus::format("${{{}}} requires a selected primary-key instance; "
                            "this configuration has none in scope",
                            std::string(access.category) + "." +
                            std::string(access.field_name))));

    key_path const field_path = pkey_container.child(std::string(access.field_name));
    const value *v = access.building.find(field_path);
    if(v == nullptr)
        return unexpected(resolve_error(resolve_errc::missing_field,
            nucleus::format("${{{}}} has no field '{}' in the selected instance",
                            access.category, access.field_name)));

    return std::string(v->text());
}

// Builds the auto-named pkey tree tokenizer for identity element `el`.
// Category = the container's last segment; the resolver reads the sliced
// keyspace anchored to the pkey container.
inline tree_tokenizer make_pkey_tree_tokenizer(const schema_element &el)
{
    std::string category = std::string(key_path::base_name(el.container().leaf()));
    key_path const pkey_container = el.container();
    std::string const identity_field = el.name;

    return tree_tokenizer(std::move(category),
        [pkey_container, identity_field](const tree_access &access) -> token_result
        { return resolve_pkey_field(pkey_container, identity_field, access); });
}

}

#endif

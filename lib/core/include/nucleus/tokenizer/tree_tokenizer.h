#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TREE_TOKENIZER_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TREE_TOKENIZER_H

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/keyspace.h"

#include "nucleus/tokenizer/tokenizer.h"

#include <functional>
#include <string>
#include <string_view>

namespace nucleus {

// Context passed to a tree-access resolver callable. All references are transient:
// valid only for the duration of the resolver call; callers must not store them.
struct tree_access
{
    const keyspace  &building;
    const key_path  &current_path;
    std::string_view category;
    std::string_view field_name;
};

// The callable a host or core supplies for tree-access token resolution.
// Invoked by tree_resolver_scope for ${category.field} in pass-2.
using tree_field_resolver = std::function<token_result(const tree_access &)>;

// A built, immutable tree-access tokenizer: a category name and a single wildcard
// resolver covering all field names under that category (${category.*}).
// Simpler than tokenizer — no named-field or function variants needed here.
class tree_tokenizer
{
public:
    explicit tree_tokenizer(std::string category, tree_field_resolver resolver)
            : m_category(std::move(category))
            , m_resolver(std::move(resolver))
    {
    }

    std::string_view category() const noexcept { return m_category; }

    bool has_empty_resolver() const noexcept { return !m_resolver; }

    token_result resolve(const tree_access &access) const
    {
        if(access.field_name.find(key_path::separator) != std::string_view::npos)
            return unexpected(resolve_error(
                    resolve_errc::missing_field,
                    "tree field '" + std::string(access.field_name) +
                            "' requires one direct child segment"));
        return m_resolver(access);
    }

private:
    std::string         m_category;
    tree_field_resolver m_resolver;
};

}

#endif

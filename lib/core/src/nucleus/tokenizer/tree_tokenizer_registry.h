#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TREE_TOKENIZER_REGISTRY_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TREE_TOKENIZER_REGISTRY_H

#include "nucleus/identity.h"

#include "nucleus/registry/registration.h"

#include "nucleus/tokenizer/tree_tokenizer.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace nucleus {

// Parallel flat registry for pass-2 tree-access tokenizers. Physically separate
// from tokenizer_registry to enforce the pass-1/pass-2 dispatch boundary: entries
// here are never reachable from the pass-1 resolver_scope. Last registration of the
// same category wins, identical to tokenizer_registry::find.
class tree_tokenizer_registry
{
public:
    tree_tokenizer_registry() = default;

    void add(tree_tokenizer tok, owner_token owner)
    {
        m_entries.push_back(make_registration(std::move(tok), std::move(owner)));
    }

    std::size_t size() const noexcept { return m_entries.size(); }

    // Returns the tokenizer registered for `category`, or nullptr. Last
    // registration wins on a duplicate category so a host override shadows an
    // earlier built-in for the same category name.
    const tree_tokenizer *find(std::string_view category) const noexcept
    {
        for(auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
            if(it->spec.category() == category)
                return &it->spec;
        return nullptr;
    }

private:
    std::vector<registration<tree_tokenizer>> m_entries;
};

}

#endif

#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_REGISTRY_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_REGISTRY_H

#include "nucleus/identity.h"

#include "nucleus/tokenizer/tokenizer.h"

#include "nucleus/registry/registration.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <string_view>

namespace nucleus {

// One of the three flat sibling registries. Holds NO reference/pointer/handle to
// any other registry; siblings are passed as parameters via the transient
// resolution context, never stored. See schema_registry for the invariant note.
//
// It stores built tokenizers (each an immutable category -> field/function
// surface) tagged with the opaque owner token, and answers a category lookup so
// the resolver can dispatch ${category....} against the matching tokenizer.
class tokenizer_registry
{
public:
    tokenizer_registry() = default;

    void add(tokenizer tok, owner_token owner)
    {
        m_entries.push_back(make_registration(std::move(tok), std::move(owner)));
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    // Returns the tokenizer registered for `category`, or nullptr. Last
    // registration wins on a duplicate category, so a host override shadows an
    // earlier built-in for the same category name.
    [[nodiscard]] const tokenizer *find(std::string_view category) const noexcept
    {
        for(auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
            if(it->spec.category() == category)
                return &it->spec;
        return nullptr;
    }

    [[nodiscard]] const std::vector<registration<tokenizer>> &entries() const noexcept
    {
        return m_entries;
    }

private:
    std::vector<registration<tokenizer>> m_entries;
};

}

#endif

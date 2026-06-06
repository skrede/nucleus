#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_REGISTRY_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_REGISTRY_H

#include "nucleus/identity.h"
#include "nucleus/registry/registration.h"

#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace nucleus {

// A minimal tokenizer registration payload. The ${...} resolution machinery
// (lexer, function invoker, resolver scope, cycle detection) lands in a later
// phase; here the spec is a stub naming the tokenizer category.
struct tokenizer_spec
{
    std::string name;
};

// One of the three flat sibling registries. Holds NO reference/pointer/handle to
// any other registry; siblings are passed as parameters via the transient
// resolution context, never stored. See schema_registry for the invariant note.
class tokenizer_registry
{
public:
    tokenizer_registry() = default;

    void add(tokenizer_spec spec, owner_token owner)
    {
        m_entries.push_back(make_registration(std::move(spec), std::move(owner)));
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    [[nodiscard]] const std::vector<registration<tokenizer_spec>> &entries() const noexcept
    {
        return m_entries;
    }

private:
    std::vector<registration<tokenizer_spec>> m_entries;
};

}

#endif

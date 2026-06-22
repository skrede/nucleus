#ifndef HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_BUILDER_H
#define HPP_GUARD_NUCLEUS_TOKENIZER_TOKENIZER_BUILDER_H

#include "nucleus/tokenizer/tokenizer.h"

#include <string>
#include <vector>
#include <utility>

namespace nucleus {

// Fluent assembler for a tokenizer. The host names a category, attaches named
// fields, named functions, and at most one wildcard field resolver, then
// consumes the builder via build() &&. Splitting construction from the immutable
// tokenizer keeps the built value free of mutation surface.
class tokenizer_builder
{
public:
    explicit tokenizer_builder(std::string category)
        : m_category(std::move(category))
    {
    }

    tokenizer_builder &add_field(std::string name, field_resolver resolve)
    {
        m_fields.push_back(token_field{std::move(name), std::move(resolve)});
        return *this;
    }

    tokenizer_builder &add_function(std::string name, std::vector<arg_spec> params,
                                    named_function_resolver resolve)
    {
        m_functions.push_back(token_function{std::move(name), std::move(params), std::move(resolve)});
        return *this;
    }

    tokenizer_builder &set_wildcard(wildcard_field_resolver resolve)
    {
        m_wildcard = std::move(resolve);
        return *this;
    }

    tokenizer build() &&
    {
        return tokenizer(std::move(m_category), std::move(m_fields),
                         std::move(m_functions), std::move(m_wildcard));
    }

private:
    std::string m_category;
    std::vector<token_field> m_fields;
    std::vector<token_function> m_functions;
    wildcard_field_resolver m_wildcard;
};

}

#endif

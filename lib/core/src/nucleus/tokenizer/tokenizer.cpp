#include "nucleus/format.h"

#include "nucleus/tokenizer/tokenizer.h"

namespace nucleus {

token_result tokenizer::resolve_field(std::string_view name) const
{
    for(const auto &field : m_fields)
        if(field.name == name)
            return field.resolve();
    if(m_wildcard)
        return m_wildcard(name);
    return unexpected(resolve_error(resolve_errc::missing_field,
                              nucleus::format("'{}' has no field '{}'", m_category, name)));
}

token_result tokenizer::resolve_function(std::string_view name,
                                         std::span<const std::string> args) const
{
    for(const auto &fn : m_functions)
        if(fn.name == name)
            return fn.resolve(args);
    return unexpected(resolve_error(resolve_errc::unknown_function,
                              nucleus::format("'{}' has no function '{}'", m_category, name)));
}

}

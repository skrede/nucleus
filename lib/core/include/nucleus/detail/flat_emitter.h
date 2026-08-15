#ifndef HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H
#define HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H

#include "nucleus/detail/flat_anchor.h"

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/ordinal_sort_key.h"

#include <span>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <string_view>

namespace nucleus::detail {

struct selected_flat_key
{
    std::string concrete;
    key_path    relative;
};

inline bool is_flat_leaf(const schema_element           &element,
                         std::span<const schema_element> elements)
{
    const std::string prefix = element.declared_path().str() + key_path::separator;
    for(const schema_element &other : elements)
        if(other.declared_path().str().starts_with(prefix))
            return false;
    return true;
}

inline bool has_flat_line_break(std::string_view text) noexcept
{
    return text.find('\n') != std::string_view::npos || text.find('\r') != std::string_view::npos;
}

inline expected<void, error> validate_flat_key(std::string_view key)
{
    if(!has_flat_line_break(key))
        return {};
    return unexpected(error{errc::malformed_source, nucleus::format("flat render: key '{}' carries an embedded newline or carriage return", key)});
}

inline expected<void, error> validate_flat_value(std::string_view key,
                                                 std::string_view value)
{
    if(!has_flat_line_break(value))
        return {};
    return unexpected(error{errc::malformed_source, nucleus::format("flat render: value for key '{}' carries an embedded newline or carriage "
                                                                    "return",
                                                                    key)});
}

inline expected<key_path, error> parse_flat_key(std::string_view key)
{
    auto parsed = key_path::parse(key);
    if(parsed)
        return std::move(parsed).value();
    return unexpected(error{errc::malformed_source, nucleus::format("flat render: key '{}' is malformed: {}", key, parsed.error())});
}

inline expected<std::vector<selected_flat_key>, error>
select_flat_keys(const config &config, const key_path &anchor)
{
    std::vector<std::string> keys = config.keys();
    std::stable_sort(keys.begin(), keys.end(), [](const std::string &left, const std::string &right)
                     { return ordinal_sort_key(left) < ordinal_sort_key(right); });
    std::vector<selected_flat_key> selected;
    for(std::string &key : keys)
    {
        auto parsed = parse_flat_key(key);
        if(!parsed)
            return unexpected(parsed.error());
        auto relative = select_flat_path(parsed.value(), anchor);
        if(!relative)
            return unexpected(relative.error());
        if(relative.value())
            selected.push_back({std::move(key), std::move(*relative.value())});
    }
    return selected;
}

inline void append_flat_line(std::string &output, std::string_view prefix,
                             std::string_view key, std::string_view value)
{
    output.append(prefix);
    output.append(key);
    output.push_back('=');
    output.append(value);
    output.push_back('\n');
}

template<typename RenderKey>
expected<std::string, error> render_flat_document(const config    &config,
                                                  std::string_view prefix, const key_path &anchor, RenderKey render_key)
{
    auto selected = select_flat_keys(config, anchor);
    if(!selected)
        return unexpected(selected.error());
    std::string output;
    for(const selected_flat_key &key : selected.value())
    {
        if(auto valid = validate_flat_key(key.concrete); !valid)
            return unexpected(valid.error());
        auto rendered = render_key(key.relative);
        if(!rendered)
            return unexpected(rendered.error());
        if(auto valid = validate_flat_key(rendered.value()); !valid)
            return unexpected(valid.error());
        for(const std::string &value : config.get_all(key.concrete))
        {
            if(auto valid = validate_flat_value(key.concrete, value); !valid)
                return unexpected(valid.error());
            append_flat_line(output, prefix, rendered.value(), value);
        }
    }
    return output;
}

inline void append_allowed_values(std::string          &output,
                                  const schema_element &element)
{
    if(element.allowed_values.empty())
        return;
    output.append(" # allowed: ");
    for(std::size_t i = 0; i < element.allowed_values.size(); ++i)
    {
        if(i != 0)
            output.push_back('|');
        output.append(element.allowed_values[i]);
    }
}

template<typename RenderKey>
expected<std::string, error> render_flat_template(const config_space &space,
                                                  std::string_view prefix, const key_path &anchor, RenderKey render_key)
{
    const std::span<const schema_element> elements = space.schema_elements();
    std::string                           output;
    for(const schema_element &element : elements)
    {
        if(!is_flat_leaf(element, elements))
            continue;
        auto relative = select_flat_path(element.declared_path(), anchor);
        if(!relative)
            return unexpected(relative.error());
        if(!relative.value())
            continue;
        auto rendered = render_key(*relative.value());
        if(!rendered)
            return unexpected(rendered.error());
        append_flat_line(output, prefix, rendered.value(), "");
        output.pop_back();
        append_allowed_values(output, element);
        output.push_back('\n');
    }
    return output;
}

}

#endif

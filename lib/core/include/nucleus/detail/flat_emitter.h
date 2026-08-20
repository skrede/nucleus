#ifndef HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H
#define HPP_GUARD_NUCLEUS_DETAIL_FLAT_EMITTER_H

#include "nucleus/detail/flat_anchor.h"
#include "nucleus/detail/flat_record.h"

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

template<typename RenderKey>
expected<flat_record, error> preflight_flat_record(
        const config &config, const selected_flat_key &key, RenderKey render_key)
{
    if(auto valid = validate_flat_key(key.concrete); !valid)
        return unexpected(valid.error());
    auto rendered = render_key(key.relative);
    if(!rendered)
        return unexpected(rendered.error());
    if(auto valid = validate_flat_key(rendered.value()); !valid)
        return unexpected(valid.error());
    std::vector<std::string> values = config.get_all(key.concrete);
    for(const std::string &value : values)
        if(auto valid = validate_flat_value(key.concrete, value); !valid)
            return unexpected(valid.error());
    return flat_record{std::move(rendered).value(), std::move(values)};
}

template<typename RenderKey>
expected<std::vector<flat_record>, error> preflight_flat_document(
        const config    &config,
        std::string_view prefix, const key_path &anchor, RenderKey render_key)
{
    if(auto valid = validate_flat_prefix(prefix); !valid)
        return unexpected(valid.error());
    auto selected = select_flat_keys(config, anchor);
    if(!selected)
        return unexpected(selected.error());
    std::vector<flat_record> records;
    records.reserve(selected.value().size());
    for(const selected_flat_key &key : selected.value())
    {
        auto record = preflight_flat_record(config, key, render_key);
        if(!record)
            return unexpected(record.error());
        records.push_back(std::move(record).value());
    }
    return records;
}

// The whole selection is proven renderable before the first byte is appended, so a
// rejected selection can never leave a partially spelled artifact behind.
template<typename RenderKey>
expected<std::string, error> render_flat_document(const config    &config,
                                                  std::string_view prefix, const key_path &anchor, RenderKey render_key)
{
    auto records = preflight_flat_document(config, prefix, anchor, render_key);
    if(!records)
        return unexpected(records.error());
    std::string output;
    for(const flat_record &record : records.value())
        for(const std::string &value : record.values)
            append_flat_line(output, prefix, record.key, value);
    return output;
}

template<typename RenderKey>
expected<flat_template_record, error> preflight_flat_template_record(
        const schema_element &element, const key_path &relative, RenderKey render_key)
{
    if(auto valid = validate_flat_key(element.declared_path().str()); !valid)
        return unexpected(valid.error());
    auto rendered = render_key(relative);
    if(!rendered)
        return unexpected(rendered.error());
    if(auto valid = validate_flat_key(rendered.value()); !valid)
        return unexpected(valid.error());
    for(const std::string &allowed : element.allowed_values)
        if(auto valid = validate_flat_value(rendered.value(), allowed); !valid)
            return unexpected(valid.error());
    return flat_template_record{std::move(rendered).value(), allowed_values_annotation(element.allowed_values)};
}

template<typename RenderKey>
expected<std::vector<flat_template_record>, error> preflight_flat_template(
        const config_space &space,
        std::string_view prefix, const key_path &anchor, RenderKey render_key)
{
    if(auto valid = validate_flat_prefix(prefix); !valid)
        return unexpected(valid.error());
    const std::span<const schema_element> elements = space.schema_elements();
    std::vector<flat_template_record>     records;
    for(const schema_element &element : elements)
    {
        if(!is_flat_leaf(element, elements))
            continue;
        auto relative = select_flat_path(element.declared_path(), anchor);
        if(!relative)
            return unexpected(relative.error());
        if(!relative.value())
            continue;
        auto record = preflight_flat_template_record(element, *relative.value(), render_key);
        if(!record)
            return unexpected(record.error());
        records.push_back(std::move(record).value());
    }
    return records;
}

template<typename RenderKey>
expected<std::string, error> render_flat_template(const config_space &space,
                                                  std::string_view prefix, const key_path &anchor, RenderKey render_key)
{
    auto records = preflight_flat_template(space, prefix, anchor, render_key);
    if(!records)
        return unexpected(records.error());
    std::string output;
    for(const flat_template_record &record : records.value())
        append_flat_line(output, prefix, record.key, record.annotation);
    return output;
}

}

#endif

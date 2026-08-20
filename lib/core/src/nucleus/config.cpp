#include "nucleus/config.h"

#include "nucleus/keyspace/storage_shape.h"

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <iterator>
#include <string_view>

namespace nucleus {
namespace {

// A key carrying `=`, CR or LF has no faithful flat spelling on any shipped
// emitter: it would move the record's key/value split or its line boundary. Such a
// key is malformed input, not an unwritable destination.
expected<void, error> reject_unrenderable_key(std::string_view key)
{
    if(key.find('=') == std::string_view::npos && key.find('\r') == std::string_view::npos && key.find('\n') == std::string_view::npos)
        return {};
    return unexpected(error{errc::malformed_source, nucleus::format("config value path '{}' carries '=', a newline or a carriage return, which "
                                                                    "no flat record can spell",
                                                                    key)});
}

expected<std::vector<key_path>, error> parse_value_paths(
        const std::map<std::string, std::string> &values)
{
    std::vector<key_path> paths;
    paths.reserve(values.size());
    for(const auto &[text, ignored] : values)
    {
        if(auto renderable = reject_unrenderable_key(text); !renderable)
            return unexpected(std::move(renderable).error());
        auto parsed = key_path::parse(text);
        if(!parsed)
            return unexpected(error{errc::malformed_source, nucleus::format("invalid config value path '{}': {}", text, parsed.error())});
        paths.push_back(std::move(parsed).value());
    }
    return paths;
}

}

config::config(std::map<std::string, std::string> values, provenance origins)
        : m_values(std::make_move_iterator(values.begin()),
                   std::make_move_iterator(values.end()))
        , m_provenance(std::move(origins))
{
}

config::config(std::map<std::string, std::string> values,
               std::map<std::string, std::any>    typed,
               provenance                         origins,
               std::vector<degradation>           degraded)
        : m_values(std::make_move_iterator(values.begin()),
                   std::make_move_iterator(values.end()))
        , m_typed(std::make_move_iterator(typed.begin()),
                  std::make_move_iterator(typed.end()))
        , m_provenance(std::move(origins))
        , m_degradations(std::move(degraded))
{
}

expected<config, error> config::from_values(
        std::map<std::string, std::string> values, provenance origins)
{
    auto paths = parse_value_paths(values);
    if(!paths)
        return unexpected(std::move(paths).error());
    if(auto shape = validate_storage_shape(paths.value()); !shape)
        return unexpected(std::move(shape).error());
    return config(std::move(values), std::move(origins));
}

config_node config::root() const noexcept
{
    return config_node{this, std::string{}};
}

std::optional<std::string> config::get(std::string_view key) const
{
    auto found = m_values.find(key);
    if(found == m_values.end())
        return std::nullopt;
    return found->second;
}

std::vector<std::string> config::get_all(std::string_view key) const
{
    auto direct = m_values.find(key);
    if(direct != m_values.end())
        return {direct->second};
    std::vector<std::pair<ordinal_key, std::string>> gathered;
    for(const auto &[path, value] : m_values)
        if(gather_path_matches(path, key))
            gathered.emplace_back(ordinal_sort_key(path), value);
    return ordered_gather(std::move(gathered));
}

bool config::contains(std::string_view key) const
{
    return m_values.contains(key);
}

const origin *config::provenance_of(std::string_view key) const
{
    return m_provenance.of(std::string(key));
}

std::size_t config::size() const noexcept
{
    return m_values.size();
}

bool config::empty() const noexcept
{
    return m_values.empty();
}

std::vector<std::string> config::keys() const
{
    std::vector<std::string> result;
    result.reserve(m_values.size());
    for(const auto &[key, ignored] : m_values)
        result.push_back(key);
    return result;
}

std::span<const degradation> config::degradations() const noexcept
{
    return m_degradations;
}

}

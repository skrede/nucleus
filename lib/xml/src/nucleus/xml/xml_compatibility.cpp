#include "nucleus/xml/xml_rendering.h"

#include "nucleus/format.h"

#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"
#include "nucleus/keyspace/ordinal_sort_key.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <algorithm>
#include <string_view>

namespace nucleus::xml {

namespace {
struct schema_role
{
    bool repeated;
    bool structural;
    bool primary;
};
struct required_primary
{
    std::string path;
    std::string parent;
    std::string name;
};
struct schema_view
{
    std::map<std::string, schema_role> roles;
    std::optional<required_primary>    required;
};
schema_view view_of(const config_space &space)
{
    schema_view view;
    const auto  elements = space.schema_elements();
    for(const schema_element &element : elements)
    {
        const std::string path = element.declared_path().str();
        view.roles.emplace(path, schema_role{element.repeated, schema_path_has_children(space, path), element.identity});
        if(element.identity && element.required)
            view.required = required_primary{path, element.container().str(),
                                             element.name};
    }
    return view;
}
expected<void, error> validate_role(const std::string &key,
                                    const std::string &segment,
                                    const std::string &path,
                                    const schema_role &role)
{
    const bool indexed = key_path::is_indexed_segment(segment);
    if(indexed && !role.repeated)
        return unexpected(xml_incompatible(key, nucleus::format("ordinal appears on non-repeated schema path '{}'", path)));
    if(!indexed && role.repeated)
        return unexpected(xml_incompatible(key, nucleus::format("repeated schema path '{}' lacks a concrete ordinal", path)));
    return {};
}
expected<bool, error> validate_schema_path(const std::string &key,
                                           const key_path    &path,
                                           const schema_view &view)
{
    std::string        canonical;
    const schema_role *leaf = nullptr;
    for(std::size_t index = 0; index < path.size(); ++index)
    {
        const std::string &segment = path.segments()[index];
        canonical                  = document_path_prefix(path.segments(), index + 1, true);
        const auto found           = view.roles.find(canonical);
        if(found == view.roles.end())
            return unexpected(xml_incompatible(key, nucleus::format("unknown schema path '{}'", canonical)));
        if(auto valid = validate_role(key, segment, canonical, found->second); !valid)
            return unexpected(std::move(valid).error());
        leaf = &found->second;
    }
    if(leaf->structural)
        return unexpected(xml_incompatible(key, nucleus::format("schema path '{}' is structural and cannot carry a scalar", canonical)));
    if(leaf->primary && path.size() == 1)
        return unexpected(xml_incompatible(key, "a primary key has no XML container"));
    return leaf->primary;
}
expected<void, error> validate_primary_presence(
        const required_primary &required, const std::set<std::string> &instances,
        const std::set<std::string> &present)
{
    for(const std::string &instance : instances)
        if(!present.contains(instance))
            return unexpected(xml_incompatible(
                    instance + '/' + required.name,
                    "required primary key is absent"));
    return {};
}
expected<void, error> validate_required_primary(
        const validated_document_plan &plan, const schema_view &view)
{
    if(!view.required)
        return {};
    auto parsed = key_path::parse(view.required->parent);
    if(!parsed)
        return unexpected(xml_incompatible(view.required->path,
                                           "primary key has no XML container"));
    const auto           &parent = parsed->segments();
    std::set<std::string> instances;
    std::set<std::string> present;
    for(const validated_document_entry &entry : plan.entries)
    {
        if(entry_matches_parent(entry, parent))
            instances.insert(document_path_prefix(entry.segments, parent.size(),
                                                  false));
        if(entry.canonical_path == view.required->path)
            present.insert(entry.parent_path);
    }
    return validate_primary_presence(*view.required, instances, present);
}
std::vector<std::string> ordered_keys(const config &config)
{
    std::vector<std::string> keys = config.keys();
    std::stable_sort(keys.begin(), keys.end(), [](const std::string &left, const std::string &right)
                     { return ordinal_sort_key(left) < ordinal_sort_key(right); });
    return keys;
}
template<typename Classifier>
expected<validated_document_plan, error> build_plan(
        const config &config, std::string_view space_name, Classifier classify)
{
    if(auto valid = validate_xml_space_name(space_name); !valid)
        return unexpected(std::move(valid).error());
    validated_document_plan plan{{}, std::string(space_name)};
    for(const std::string &key : ordered_keys(config))
    {
        auto path = key_path::parse(key);
        if(!path)
            return unexpected(xml_incompatible(key, path.error()));
        auto primary = classify(key, path.value());
        if(!primary)
            return unexpected(std::move(primary).error());
        auto entry = make_document_entry(config, key, path.value(), primary.value());
        if(!entry)
            return unexpected(std::move(entry).error());
        plan.entries.push_back(std::move(entry).value());
    }
    if(auto valid = validate_primary_cardinality(plan); !valid)
        return unexpected(std::move(valid).error());
    return plan;
}

}

expected<validated_document_plan, error> validate_document(
        const config &config, const config_space &space,
        std::string_view space_name)
{
    const schema_view view = view_of(space);
    auto              plan = build_plan(config, space_name,
                                        [&view](const std::string &key, const key_path &path)
                                        { return validate_schema_path(key, path, view); });
    if(!plan)
        return unexpected(std::move(plan).error());
    if(auto valid = validate_required_primary(plan.value(), view); !valid)
        return unexpected(std::move(valid).error());
    return plan;
}

expected<validated_document_plan, error> validate_document(
        const config &config, const schema_projection &projection,
        std::string_view space_name)
{
    return build_plan(config, space_name,
                      [&projection](const std::string &key, const key_path &path)
                              -> expected<bool, error>
                      {
                          const auto       &segments  = path.segments();
                          const std::string canonical = document_path_prefix(
                                  segments, segments.size(), true);
                          if(projection.is_repeated_container(canonical))
                              return unexpected(xml_incompatible(key,
                                                                 "a repeated structural container carries a scalar"));
                          const std::string parent = document_path_prefix(
                                  segments, segments.size() - 1, true);
                          const std::string *primary = projection.key_of(parent);
                          return primary != nullptr && *primary == key_path::base_name(segments.back());
                      });
}

expected<validated_document_plan, error> validate_document_schema_blind(
        const config &config, std::string_view space_name)
{
    return build_plan(config, space_name,
                      [](const std::string &, const key_path &)
                              -> expected<bool, error>
                      { return false; });
}

}

#include "nucleus/xml/xml_grammar.h"
#include "nucleus/xml/xml_rendering.h"

#include "nucleus/format.h"

#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace nucleus::xml {

namespace {

pugi::xml_node child_or_append(pugi::xml_node parent, const std::string &name)
{
    const pugi::xml_node existing = parent.child(name.c_str());
    if(existing)
        return existing;
    return parent.append_child(name.c_str());
}

expected<pugi::xml_node, error> indexed_child(
        pugi::xml_node parent, const std::string &name, std::uint64_t ordinal,
        const std::string &path)
{
    std::uint64_t count = 0;
    for(pugi::xml_node child = parent.first_child(); child;
        child                = child.next_sibling())
    {
        if(std::string(child.name()) != name)
            continue;
        if(count == ordinal)
            return child;
        ++count;
    }
    if(count == ordinal)
        return parent.append_child(name.c_str());
    return unexpected(xml_incompatible(path, nucleus::format("repeated container has an ordinal gap before instance {}", ordinal)));
}

void append_value(pugi::xml_node node, const std::string &value)
{
    const bool whitespace = !value.empty() &&
            value.find_first_not_of(" \t\n") == std::string::npos;
    node.append_child(whitespace ? pugi::node_cdata : pugi::node_pcdata)
            .set_value(value.c_str());
}

expected<pugi::xml_node, error> entry_parent(
        pugi::xml_document &document, const validated_document_entry &entry)
{
    pugi::xml_node parent = document.root();
    for(std::size_t index = 0; index + 1 < entry.segments.size(); ++index)
    {
        const std::string &segment = entry.segments[index];
        const std::string  name(key_path::base_name(segment));
        if(key_path::is_indexed_segment(segment))
        {
            auto child = indexed_child(parent, name, key_path::ordinal_of(segment),
                                       document_path_prefix(entry.segments,
                                                            index + 1, false));
            if(!child)
                return unexpected(std::move(child).error());
            parent = child.value();
        }
        else
            parent = child_or_append(parent, name);
    }
    return parent;
}

expected<void, error> append_elements(
        pugi::xml_node parent, const validated_document_entry &entry,
        const std::string &segment, const std::string &name)
{
    for(const std::string &value : entry.values)
    {
        if(key_path::is_indexed_segment(segment))
        {
            auto child = indexed_child(parent, name,
                                       key_path::ordinal_of(segment), entry.key);
            if(!child)
                return unexpected(std::move(child).error());
            append_value(child.value(), value);
        }
        else
            append_value(parent.append_child(name.c_str()), value);
    }
    return {};
}

expected<void, error> append_entry(pugi::xml_document             &document,
                                   const validated_document_entry &entry)
{
    auto parent = entry_parent(document, entry);
    if(!parent)
        return unexpected(std::move(parent).error());
    const std::string &segment = entry.segments.back();
    const std::string  name(key_path::base_name(segment));
    if(entry.representation == xml_representation::attribute)
    {
        parent->append_attribute(name.c_str()).set_value(entry.values.front().c_str());
        return {};
    }
    return append_elements(parent.value(), entry, segment, name);
}

std::size_t root_count(const pugi::xml_document &document)
{
    std::size_t count = 0;
    for(pugi::xml_node child = document.first_child(); child;
        child                = child.next_sibling())
        if(child.type() == pugi::node_element)
            ++count;
    return count;
}

void wrap_roots(pugi::xml_document &document, const std::string &name)
{
    std::vector<pugi::xml_node> roots;
    for(pugi::xml_node child = document.first_child(); child;
        child                = child.next_sibling())
        roots.push_back(child);
    pugi::xml_node wrapper = document.append_child(name.c_str());
    for(const pugi::xml_node root : roots)
        wrapper.append_move(root);
}

}

bool entry_matches_parent(const validated_document_entry &entry,
                          const std::vector<std::string> &parent)
{
    if(entry.segments.size() <= parent.size())
        return false;
    for(std::size_t index = 0; index < parent.size(); ++index)
        if(key_path::base_name(entry.segments[index]) != parent[index])
            return false;
    return true;
}

expected<validated_document_entry, error> make_document_entry(
        const config &config, const std::string &key, const key_path &path,
        bool primary)
{
    if(auto valid = validate_xml_name_segments(key, path.segments()); !valid)
        return unexpected(std::move(valid).error());
    std::vector<std::string> values = config.get_all(key);
    if(auto valid = validate_xml_stored_values(key, values); !valid)
        return unexpected(std::move(valid).error());
    const auto &segments = path.segments();
    return validated_document_entry{key, std::move(values), segments,
                                    document_path_prefix(segments, segments.size(), true),
                                    document_path_prefix(segments, segments.size() - 1, false),
                                    primary ? xml_representation::attribute
                                            : xml_representation::element};
}

expected<void, error> validate_primary_cardinality(
        const validated_document_plan &plan)
{
    std::set<std::string> parents;
    for(const validated_document_entry &entry : plan.entries)
    {
        if(entry.representation != xml_representation::attribute)
            continue;
        if(entry.values.size() != 1 || !parents.insert(entry.parent_path).second)
            return unexpected(xml_incompatible(
                    entry.key, "primary-key container carries more than one value"));
    }
    return {};
}

expected<std::string, error> render_validated_document(
        const validated_document_plan &plan)
{
    pugi::xml_document document;
    for(const validated_document_entry &entry : plan.entries)
        if(auto appended = append_entry(document, entry); !appended)
            return unexpected(std::move(appended).error());
    std::string wrapper(plan.space_name);
    if(wrapper.empty() && root_count(document) != 1)
        wrapper = "config";
    if(!wrapper.empty())
        wrap_roots(document, wrapper);
    return serialize_xml(document, pugi::format_default);
}

}

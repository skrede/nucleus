#include "nucleus/xml/xml_grammar.h"
#include "nucleus/xml/xml_rendering.h"

#include "nucleus/format.h"

#include "nucleus/schema/schema.h"

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <string_view>

namespace nucleus::xml {

namespace {

class string_writer final : public pugi::xml_writer
{
public:
    explicit string_writer(std::string &output)
            : m_output(output)
    {
    }

    void write(const void *data, std::size_t size) override
    {
        m_output.append(static_cast<const char *>(data), size);
    }

private:
    std::string &m_output;
};

pugi::xml_node child_or_append(pugi::xml_node parent, const std::string &name)
{
    const pugi::xml_node existing = parent.child(name.c_str());
    if(existing)
        return existing;
    return parent.append_child(name.c_str());
}

std::string join_values(const std::vector<std::string> &values)
{
    std::string joined;
    for(const std::string &value : values)
    {
        if(!joined.empty())
            joined.push_back('|');
        joined += value;
    }
    return joined;
}

void append_template_element(pugi::xml_document   &document,
                             const schema_element &element)
{
    pugi::xml_node                 current  = document.root();
    const std::vector<std::string> segments = element.declared_path().segments();
    for(const std::string &segment : segments)
        current = child_or_append(current, segment);
    if(!element.allowed_values.empty())
        current.append_attribute("allowed").set_value(
                join_values(element.allowed_values).c_str());
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

expected<void, error> validate_template_element(const schema_element &element)
{
    const key_path    declared = element.declared_path();
    const std::string subject  = declared.str();
    // A declared path is an anchor path: it names elements, never instances, so the
    // template emits each segment verbatim and validates the same text it emits. The
    // document surface reads base_name instead because its input is an instance path.
    for(const std::string &segment : declared.segments())
        if(!is_valid_xml_name(segment))
            return unexpected(xml_grammar_error(subject, nucleus::format("segment '{}' is not a valid XML name", segment)));
    return validate_xml_annotations(subject, element.allowed_values);
}

// The public template surface emits the whole schema, so invalid content anywhere
// in it blocks success rather than being skipped over.
expected<void, error> validate_template(const config_space &space,
                                        std::string_view    space_name)
{
    if(auto valid = validate_xml_space_name(space_name); !valid)
        return unexpected(std::move(valid).error());
    for(const schema_element &element : space.schema_elements())
        if(auto valid = validate_template_element(element); !valid)
            return unexpected(std::move(valid).error());
    return {};
}

}

error xml_incompatible(const std::string &key, std::string reason)
{
    return error{errc::malformed_source,
                 nucleus::format("xml emit: key '{}': {}", key, reason)};
}

bool schema_path_has_children(const config_space &space,
                              const std::string  &path)
{
    const auto elements = space.schema_elements();
    return std::any_of(elements.begin(), elements.end(),
                       [&path](const schema_element &child)
                       { return child.container().str() == path; });
}

std::string document_path_prefix(const std::vector<std::string> &segments,
                                 std::size_t count, bool canonical)
{
    std::string result;
    for(std::size_t index = 0; index < count; ++index)
    {
        if(!result.empty())
            result.push_back(key_path::separator);
        const std::string_view segment = canonical
                ? key_path::base_name(segments[index])
                : segments[index];
        result.append(segment);
    }
    return result;
}

expected<void, error> validate_xml_space_name(std::string_view space_name)
{
    if(!space_name.empty() && !is_valid_xml_name(space_name))
        return unexpected(error{errc::malformed_source, nucleus::format("xml emit: space name '{}' is not a valid XML name", space_name)});
    return {};
}

expected<std::string, error> serialize_xml(const pugi::xml_document &document,
                                           unsigned int              flags)
{
    std::string   output;
    string_writer writer(output);
    document.save(writer, "  ", flags, pugi::encoding_utf8);
    return output;
}

expected<std::string, error> render_xml_template(
        const config_space &space, std::string_view space_name)
{
    if(auto valid = validate_template(space, space_name); !valid)
        return unexpected(std::move(valid).error());
    pugi::xml_document document;
    for(const schema_element &element : space.schema_elements())
        append_template_element(document, element);
    std::string wrapper(space_name);
    if(wrapper.empty() && root_count(document) != 1)
        wrapper = "config";
    if(!wrapper.empty())
        wrap_roots(document, wrapper);
    return serialize_xml(document,
                         pugi::format_default | pugi::format_no_declaration);
}

}

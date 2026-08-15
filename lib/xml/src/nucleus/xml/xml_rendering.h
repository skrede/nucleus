#ifndef HPP_GUARD_NUCLEUS_XML_XML_RENDERING_H
#define HPP_GUARD_NUCLEUS_XML_XML_RENDERING_H

#include "nucleus/error.h"
#include "nucleus/config.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/projection.h"

#include "nucleus/keyspace/key_path.h"

#include <pugixml.hpp>

#include <string>
#include <vector>
#include <cstddef>
#include <string_view>

namespace nucleus::xml {

enum class xml_representation
{
    element,
    attribute,
};

struct validated_document_entry
{
    std::string              key;
    std::vector<std::string> values;
    std::vector<std::string> segments;
    std::string              canonical_path;
    std::string              parent_path;
    xml_representation       representation;
};

struct validated_document_plan
{
    std::vector<validated_document_entry> entries;
    std::string                           space_name;
};

error xml_incompatible(const std::string &key, std::string reason);

bool is_valid_xml_name(std::string_view name);

bool is_forbidden_xml_value(char byte);

bool schema_path_has_children(const config_space &space,
                              const std::string  &path);

bool entry_matches_parent(const validated_document_entry &entry,
                          const std::vector<std::string> &parent);

std::string document_path_prefix(const std::vector<std::string> &segments,
                                 std::size_t count, bool canonical);

expected<void, error> validate_xml_space_name(std::string_view space_name);

expected<validated_document_entry, error> make_document_entry(
        const config &config, const std::string &key, const key_path &path,
        bool primary);

expected<void, error> validate_primary_cardinality(
        const validated_document_plan &plan);

expected<std::string, error> serialize_xml(const pugi::xml_document &document,
                                           unsigned int              flags);

expected<std::string, error> render_xml_template(
        const config_space &space, std::string_view space_name);

expected<validated_document_plan, error> validate_document(
        const config &config, const config_space &space,
        std::string_view space_name);

expected<validated_document_plan, error> validate_document(
        const config &config, const schema_projection &projection,
        std::string_view space_name);

expected<validated_document_plan, error> validate_document_schema_blind(
        const config &config, std::string_view space_name);

expected<std::string, error> render_validated_document(
        const validated_document_plan &plan);

}

#endif

#ifndef HPP_GUARD_NUCLEUS_XML_XML_EMITTER_H
#define HPP_GUARD_NUCLEUS_XML_XML_EMITTER_H

#include "nucleus/error.h"
#include "nucleus/expected.h"

#include "nucleus/schema/projection.h"

#include <iosfwd>
#include <string>
#include <utility>
#include <string_view>

namespace nucleus {

class config;
class config_space;

namespace xml {

expected<std::string, error> render_template(
        const config_space &space, std::string_view space_name = {});

expected<std::string, error> render_document(
        const config &config, const config_space &space,
        std::string_view space_name = {});

expected<std::string, error> render_document_schema_blind(
        const config &config, std::string_view space_name = {});

expected<void, error> emit_template(
        const config_space &space, std::ostream &out,
        std::string_view space_name = {});

expected<void, error> emit_document(
        const config &config, const config_space &space, std::ostream &out,
        std::string_view space_name = {});

expected<void, error> emit_document_schema_blind(
        const config &config, std::ostream &out,
        std::string_view space_name = {});

expected<void, error> emit_document(
        const config &config, std::ostream &out,
        std::string_view space_name = {});

expected<void, error> emit_document(
        const config &config, std::ostream &out,
        const schema_projection &projection,
        std::string_view         space_name = {});

class emitter
{
public:
    emitter()
            : m_space_name()
    {
    }

    explicit emitter(std::string space_name)
            : m_space_name(std::move(space_name))
    {
    }

    expected<std::string, error> render_template(
            const config_space &space) const
    {
        return nucleus::xml::render_template(space, m_space_name);
    }

    expected<std::string, error> render_document(
            const config &config, const config_space &space) const
    {
        return nucleus::xml::render_document(config, space, m_space_name);
    }

    expected<void, error> emit_template(
            const config_space &space, std::ostream &out) const
    {
        return nucleus::xml::emit_template(space, out, m_space_name);
    }

    expected<void, error> emit_document(
            const config &config, const config_space &space,
            std::ostream &out) const
    {
        return nucleus::xml::emit_document(config, space, out, m_space_name);
    }

private:
    std::string m_space_name;
};

}

}

#endif

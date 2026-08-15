#include "nucleus/xml/xml_emitter.h"
#include "nucleus/xml/xml_rendering.h"

#include "nucleus/config_emitter.h"

#include "nucleus/detail/emitter_delivery.h"

#include <string>
#include <ostream>
#include <utility>
#include <string_view>

namespace nucleus::xml {

namespace {

expected<std::string, error> render_plan(
        expected<validated_document_plan, error> plan)
{
    if(!plan)
        return unexpected(std::move(plan).error());
    return render_validated_document(plan.value());
}

}

expected<std::string, error> render_template(
        const config_space &space, std::string_view space_name)
{
    return render_xml_template(space, space_name);
}

expected<std::string, error> render_document(
        const config &config, const config_space &space,
        std::string_view space_name)
{
    return render_plan(validate_document(config, space, space_name));
}

expected<std::string, error> render_document_schema_blind(
        const config &config, std::string_view space_name)
{
    return render_plan(validate_document_schema_blind(config, space_name));
}

expected<void, error> emit_template(
        const config_space &space, std::ostream &out,
        std::string_view space_name)
{
    return detail::deliver_rendered(render_template(space, space_name), out);
}

expected<void, error> emit_document(
        const config &config, const config_space &space, std::ostream &out,
        std::string_view space_name)
{
    return detail::deliver_rendered(
            render_document(config, space, space_name), out);
}

expected<void, error> emit_document_schema_blind(
        const config &config, std::ostream &out,
        std::string_view space_name)
{
    return detail::deliver_rendered(
            render_document_schema_blind(config, space_name), out);
}

static_assert(config_emitter<emitter>);

}

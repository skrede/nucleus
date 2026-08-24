// emit_template: project a schema into an XML template.
//
// The output seam is format-agnostic: core exposes the declared schema, and each
// format module supplies an emitter. nucleus::xml::render_template turns the schema
// into a well-formed XML template -- one element per field, nested by anchor path,
// with constrained fields annotated by their allowed values -- and hands back owned
// storage rather than writing into a caller's stream, so a failure costs the caller
// nothing already printed. This example links the xml module (nucleus::xml); its
// header under nucleus/xml/ arrives via that link, pugixml stays private.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_emitter.h"

#include <string>
#include <vector>
#include <utility>
#include <iostream>

namespace {

// A `server` container primary-keyed by `name`, a unique-named `profile`, and a
// constrained `mode` leaf.
nucleus::expected<nucleus::config_space, nucleus::error> make_space()
{
    nucleus::config_space_builder builder;
    if(auto result = builder.register_element(nucleus::element("server", nucleus::anchor::root())); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.register_element(
               nucleus::primary_key_element("name", nucleus::anchor::keyspace("server")));
       !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.register_element(
               nucleus::unique_element("profile", nucleus::anchor::keyspace("server")));
       !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = builder.register_element(
               nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                                     std::vector<std::string>{"primary", "secondary"}));
       !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

}

int main()
{
    const auto sealed = make_space();
    if(!sealed)
    {
        std::cerr << "space setup failed: " << sealed.error() << '\n';
        return 1;
    }
    const auto rendered = nucleus::xml::render_template(sealed.value());
    if(!rendered)
    {
        std::cerr << "render failed: " << rendered.error() << '\n';
        return 1;
    }
    std::cout << rendered.value();
    return 0;
}

// emit_template: project a schema into an XML template, written to a stream.
//
// The output seam is format-agnostic: core exposes the declared schema, and each
// format module supplies an emitter. nucleus::xml::emit_template turns the schema
// into a well-formed XML template -- one element per field, nested by anchor path,
// with constrained fields annotated by their allowed values -- and writes it into
// the caller's stream. This example links the xml module (nucleus::xml); its
// header under nucleus/xml/ arrives via that link, pugixml stays private.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_emitter.h"

#include <vector>
#include <iostream>

int main()
{
    nucleus::config_space_builder builder;

    // A `server` container primary-keyed by `name`, a unique-named `profile`, and a
    // constrained `mode` leaf.
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(
        nucleus::unique_element("profile", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(nucleus::enum_element(
        "mode", nucleus::anchor::keyspace("server"),
        std::vector<std::string>{"primary", "secondary"})))
        return 1;

    nucleus::config_space space = builder.build();
    nucleus::xml::emit_template(space, std::cout);
    return 0;
}

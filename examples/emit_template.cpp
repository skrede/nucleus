// emit_template: project a schema into an XML template string.
//
// emit_xml_template() turns the declared schema into a well-formed XML template:
// one element per field, nested by anchor path, with constrained fields annotated
// by their allowed values. It is hand-built in core (no XML library), so this
// example links ONLY nucleus::nucleus -- the optional XML source module is not
// needed to emit a template.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <vector>
#include <iostream>

int main()
{
    nucleus::configuration_space_builder builder;

    // A `server` container primary-keyed by `name`, a unique-named `profile`, and a
    // constrained `mode` leaf.
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server")));
    builder.register_element(
        nucleus::unique_element("profile", nucleus::anchor::keyspace("server")));
    builder.register_element(nucleus::enum_element(
        "mode", nucleus::anchor::keyspace("server"),
        std::vector<std::string>{"primary", "secondary"}));

    nucleus::configuration_space space = builder.build();
    std::cout << space.emit_xml_template();
    return 0;
}

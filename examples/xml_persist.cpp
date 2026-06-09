// xml_persist: load a configuration from XML, then persist it back to XML.
//
// nucleus::xml::emit_document takes a resolved configuration and writes a well-formed
// XML document into the caller's stream via the pugixml write API -- entirely inside
// the xml module, so no pugixml type crosses into core. A repeated leaf persists all
// its values. This is the resolved-config -> XML direction (the inverse of reading);
// the user owns persistence (here, std::cout).

#include "nucleus/configuration_space.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/sources/xml_source.h"
#include "nucleus/sources/xml_emitter.h"

#include <memory>
#include <string>
#include <iostream>

int main()
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server")));
    builder.register_element(
        nucleus::repeated_element("tag", nucleus::anchor::keyspace("server")));
    nucleus::configuration_space space = builder.build();

    const char *document =
        "<server><host>localhost</host><tag>alpha</tag><tag>beta</tag></server>";
    auto make = [document](const std::string &) -> std::unique_ptr<nucleus::configuration_source> {
        return std::make_unique<nucleus::xml::xml_source>(
            nucleus::xml::xml_source::from(nucleus::xml::xml_source_options::of_string(document)));
    };

    nucleus::source_stack_options options;
    options.document_paths = {"config.xml"};
    options.make_document = make;

    auto loaded = nucleus::load_configuration(space, options);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    nucleus::xml::emit_document(loaded.value(), std::cout);
    return 0;
}

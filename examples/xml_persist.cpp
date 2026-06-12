// xml_persist: load a config from XML, then persist it back to XML.
//
// nucleus::xml::emit_document takes a resolved config and writes a well-formed
// XML document into the caller's stream via the pugixml write API -- entirely inside
// the xml module, so no pugixml type crosses into core. A repeated leaf persists all
// its values. This is the resolved-config -> XML direction (the inverse of reading);
// the user owns persistence (here, std::cout).

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include <string>
#include <iostream>

int main()
{
    nucleus::config_space_builder builder;
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(
        nucleus::repeated_element("tag", nucleus::anchor::keyspace("server"))))
        return 1;
    nucleus::config_space space = builder.build();

    const char *document =
        "<server><host>localhost</host><tag>alpha</tag><tag>beta</tag></server>";
    auto make = [document](const std::string &) -> nucleus::source_handle {
        return nucleus::source_handle(
            nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };

    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
        nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    nucleus::xml::emit_document(loaded.value(), std::cout);
    return 0;
}

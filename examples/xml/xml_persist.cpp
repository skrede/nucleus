// xml_persist: load a config from XML, then persist it back to XML.
//
// nucleus::xml::render_document takes a resolved config together with the space that
// declared it and builds a well-formed XML document through the pugixml write API --
// entirely inside the xml module, so no pugixml type crosses into core. The space is
// what lets the emitter distinguish a repeated leaf from a scalar one, so all of a
// repeated leaf's values persist. This is the resolved-config -> XML direction (the
// inverse of reading); the document comes back as owned storage and the user owns
// persistence (here, std::cout).

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include <string>
#include <iostream>

namespace {

// A server container with a host leaf and a repeated tag leaf.
bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(
                   nucleus::element("server", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::element("host", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::repeated_element("tag", nucleus::anchor::keyspace("server")));
}

nucleus::load_result load_values(const nucleus::config_space &space)
{
    const std::string document =
            "<server><host>localhost</host><tag>alpha</tag><tag>beta</tag></server>";
    nucleus::load_options options;
    options.document_paths = {"config.xml"};
    options.make_document  = [document](const std::string &) -> nucleus::source_handle
    {
        return nucleus::source_handle(nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };
    return nucleus::load_config(space, nucleus::source_stack{}, options);
}

}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const auto sealed = builder.build();
    if(!sealed)
        return 1;
    const nucleus::config_space &space = sealed.value();
    const nucleus::load_result  loaded = load_values(space);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const auto rendered = nucleus::xml::render_document(loaded.value(), space);
    if(!rendered)
    {
        std::cerr << "render failed: " << rendered.error() << '\n';
        return 1;
    }
    std::cout << rendered.value();
    return 0;
}

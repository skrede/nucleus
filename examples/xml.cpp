// xml: layer an XML document under the command line onto one keyspace.
//
// An XML document is a source like any other: nested elements become `/`-paths
// and attributes become values. The document factory turns a path into a source
// so the core never knows a file format. argv outranks the document, so it
// overrides `mode` while the document's `host` survives.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <memory>
#include <vector>
#include <string>
#include <iostream>

int main()
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::required_element("host", nucleus::anchor::keyspace("server")));
    builder.register_element(
        nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                              {"http", "https"}));
    nucleus::configuration_space space = builder.build();

    // Parse from an in-memory string so the example needs no file on disk. The
    // document factory builds the xml_source from an xml_source_options value --
    // the only seam through which the xml module reaches the core.
    const char *document = R"(<server host="127.0.0.1" mode="http"/>)";
    auto make = [document](const std::string &) -> std::unique_ptr<nucleus::configuration_source> {
        return std::make_unique<nucleus::xml::xml_source>(
            nucleus::xml::xml_source::from(nucleus::xml::xml_source_options::of_string(document)));
    };

    // argv outranks the document band, so it overrides `mode`; the document's
    // `host` survives.
    nucleus::source_stack_options options;
    options.argv = nucleus::argv_source_options{{"--server-mode=https"}};
    options.document_paths = {"config.xml"};
    options.make_document = make;

    auto loaded = nucleus::load_configuration(space, options);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
    return 0;
}

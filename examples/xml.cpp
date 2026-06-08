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
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("server", nucleus::anchor::root()));
    engine.register_element(
        nucleus::required_element("host", nucleus::anchor::keyspace("server")));
    engine.register_element(
        nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                              {"http", "https"}));

    // Parse from an in-memory string so the example needs no file on disk.
    const char *document = R"(<server host="127.0.0.1" mode="http"/>)";
    auto make = [document](const std::string &) -> std::unique_ptr<nucleus::configuration_source> {
        return std::make_unique<nucleus::xml::xml_source>(
            nucleus::xml::xml_source::from_string(document));
    };

    auto loaded = engine.load(std::vector<std::string>{"--server-mode=https"},
                              std::vector<std::string>{"config.xml"}, make);
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

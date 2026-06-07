// strains: ship two named strains in one document, then select one by
// primary-key value. The resolved keyspace never contains the key segment.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <memory>
#include <string>
#include <vector>
#include <iostream>

int main()
{
    // Schema: cluster/server keyed by name, leaves port and protocol.
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("cluster", nucleus::anchor::root()));
    engine.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster")));
    engine.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("port", nucleus::anchor::keyspace("cluster/server")));
    engine.register_element(
        nucleus::element("protocol", nucleus::anchor::keyspace("cluster/server")));

    // Select the "yin" strain before load. Only one named instance survives.
    if(auto selected = engine.select("yin"); !selected)
    {
        std::cerr << "select failed: " << selected.error() << '\n';
        return 1;
    }

    // One document containing an anonymous template (protocol) and two named
    // strains (yin and yang). No file on disk: the factory ignores the path
    // and returns this in-memory string every time.
    const char *document = R"(
        <cluster>
            <server><protocol>tcp</protocol></server>
            <server name="yin"><port>8080</port></server>
            <server name="yang"><port>22</port></server>
        </cluster>)";

    auto make = [document](const std::string &) -> std::unique_ptr<nucleus::source> {
        return std::make_unique<nucleus::xml::xml_source>(
            nucleus::xml::xml_source::from_string(document));
    };

    auto loaded = engine.load(std::vector<std::string>{"config.xml"}, make);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();

    // Print all resolved keys. The anonymous template's protocol and yin's
    // port both appear at the unified cluster/server/* path.
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';

    // The primary-key value "yin" is stripped from the resolved keyspace.
    // The path cluster/server/yin/port never exists after resolve.
    if(!config.contains("cluster/server/yin/port"))
        std::cout << "cluster/server/yin/port is absent (key segment stripped)\n";

    return 0;
}

// strains: ship two named strains in one document, then select one by
// primary-key value. The resolved keyspace never contains the key segment.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <string>
#include <iostream>

int main()
{
    // Schema: cluster/server keyed by name, leaves port and protocol.
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("cluster", nucleus::anchor::root()));
    builder.register_element(nucleus::element("server", nucleus::anchor::keyspace("cluster")));
    builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server")));
    builder.register_element(
        nucleus::element("port", nucleus::anchor::keyspace("cluster/server")));
    builder.register_element(
        nucleus::element("protocol", nucleus::anchor::keyspace("cluster/server")));
    nucleus::configuration_space space = builder.build();

    // One document containing an anonymous template (protocol) and two named
    // strains (primary and secondary). No file on disk: the factory ignores the path
    // and returns this in-memory string every time.
    const char *document = R"(
        <cluster>
            <server><protocol>tcp</protocol></server>
            <server name="primary"><port>8080</port></server>
            <server name="secondary"><port>22</port></server>
        </cluster>)";

    auto make = [document](const std::string &) -> nucleus::source_handle {
        return nucleus::source_handle(
            nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };

    // Select the "primary" strain for this load -- a per-load parameter.
    auto loaded = nucleus::load(space, nucleus::source_stack{},
        nucleus::load_options{
            .selection = "primary",
            .document_paths = {"config.xml"},
            .make_document = make});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();

    // Print all resolved keys. The anonymous template's protocol and primary's
    // port both appear at the unified cluster/server/* path.
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';

    // The primary-key value "primary" is stripped from the resolved keyspace.
    // The path cluster/server/primary/port never exists after resolve.
    if(!config.contains("cluster/server/primary/port"))
        std::cout << "cluster/server/primary/port is absent (key segment stripped)\n";

    return 0;
}

// strains: ship two named strains in one document, then select one by
// primary-key value. The resolved keyspace never contains the key segment.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/xml/xml_source.h"

#include <string>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("cluster", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::element("server", nucleus::anchor::keyspace("cluster"))) &&
            builder.register_element(
                    nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server"))) &&
            builder.register_element(
                    nucleus::element("port", nucleus::anchor::keyspace("cluster/server"))) &&
            builder.register_element(
                    nucleus::element("protocol", nucleus::anchor::keyspace("cluster/server")));
}

// One document containing an anonymous template (protocol) and two named
// strains (primary and secondary). No file on disk: the factory ignores the path
// and returns this in-memory string every time.
static nucleus::source_handle make_document(const std::string &)
{
    const char *document = R"(
        <cluster>
            <server><protocol>tcp</protocol></server>
            <server name="primary"><port>8080</port></server>
            <server name="secondary"><port>22</port></server>
        </cluster>)";
    return nucleus::source_handle(
            nucleus::xml_source::from(
                    nucleus::xml_source_options::of_string(document)));
}

// Print all resolved keys. The anonymous template's protocol and primary's
// port both appear at the unified cluster/server/* path. The selected strain's
// key value is retained as a readable leaf.
static void print_selected(const nucleus::config &config)
{
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
    if(const auto name = config.get("cluster/server/name"))
        std::cout << "cluster/server/name = " << *name << '\n';
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

    // Select the "primary" strain for this load -- a per-load parameter.
    auto loaded = nucleus::load_config(space, nucleus::source_stack{},
                                       nucleus::load_options{
                                               .selection      = "primary",
                                               .document_paths = {"config.xml"},
                                               .make_document  = make_document});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    print_selected(loaded.value());
    return 0;
}

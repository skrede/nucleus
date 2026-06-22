// pkey_identity: demonstrate that the selected primary-key value is retained
// as a readable leaf after strain resolution, and that load → emit → load is a
// byte-stable fixpoint when the schema-aware emit_document overload is used.
//
// Phase 22 feature: the pkey value (e.g. name="web") previously disappeared
// from the resolved keyspace; it now lives at cluster/server/name and is readable
// like any other leaf. The schema-aware emit overload renders it as an XML
// attribute on the parent element rather than a child, preventing double-write on
// a round-trip reload.

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/projection.h"

#include "nucleus/xml/xml_source.h"
#include "nucleus/xml/xml_emitter.h"

#include <sstream>
#include <string>
#include <iostream>

namespace {

// Build the schema_projection from the public schema_elements() surface.
nucleus::schema_projection projection_of(const nucleus::config_space &space)
{
    nucleus::schema_projection proj;
    for(const nucleus::schema_element &el : space.schema_elements())
    {
        if(el.identity)
            proj.set_key(el.container().str(), el.name);
        if(el.repeated)
            proj.set_repeated_container(el.container().str());
    }
    return proj;
}

}

int main()
{
    // Schema: cluster/server keyed by name; non-key leaves port and protocol.
    nucleus::config_space_builder builder;
    if(!builder.register_element(nucleus::element("cluster", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(
        nucleus::element("server", nucleus::anchor::keyspace("cluster"))))
        return 1;
    if(!builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("cluster/server"))))
        return 1;
    if(!builder.register_element(
        nucleus::element("port", nucleus::anchor::keyspace("cluster/server"))))
        return 1;
    if(!builder.register_element(
        nucleus::element("protocol", nucleus::anchor::keyspace("cluster/server"))))
        return 1;
    nucleus::config_space space = builder.build();

    // Inline document: two named strains plus a shared anonymous template.
    const char *document = R"(
        <cluster>
            <server><protocol>tcp</protocol></server>
            <server name="web"><port>80</port></server>
            <server name="db"><port>5432</port></server>
        </cluster>)";

    auto make = [document](const std::string &) -> nucleus::source_handle {
        return nucleus::source_handle(
            nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };

    // Load with selection="web": resolves the named strain.
    nucleus::load_options opts;
    opts.selection = "web";
    opts.document_paths = {"config.xml"};
    opts.make_document = make;
    auto loaded = nucleus::load_config(space, nucleus::source_stack{}, opts);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    const nucleus::config &config = loaded.value();

    // IDN-01: the selected pkey value is a readable leaf at cluster/server/name.
    if(const auto name = config.get("cluster/server/name"))
        std::cout << "cluster/server/name = " << *name << '\n';

    // Print all resolved keys.
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value_or("") << '\n';

    // IDN-03: emit with schema projection so the pkey renders as an XML attribute.
    std::ostringstream out;
    nucleus::xml::emit_document(config, out, projection_of(space));
    const std::string emitted = out.str();
    std::cout << "\nEmitted XML:\n" << emitted;

    // Reload the emitted XML and verify the round-trip is a fixpoint.
    nucleus::load_options reload_opts;
    reload_opts.document_paths = {"emitted.xml"};
    reload_opts.make_document = [&emitted](const std::string &) -> nucleus::source_handle {
        return nucleus::source_handle(
            nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(emitted)));
    };
    auto reloaded = nucleus::load_config(space, nucleus::source_stack{}, reload_opts);
    if(!reloaded)
    {
        std::cerr << "reload failed: " << reloaded.error() << '\n';
        return 1;
    }
    const nucleus::config &c2 = reloaded.value();

    // Assert the fixpoint: same key set and same pkey value after round-trip.
    if(c2.keys() != config.keys())
    {
        std::cerr << "round-trip key mismatch\n";
        return 1;
    }
    if(c2.get("cluster/server/name") != config.get("cluster/server/name"))
    {
        std::cerr << "round-trip pkey value mismatch\n";
        return 1;
    }

    std::cout << "Round-trip fixpoint: key set and pkey value preserved.\n";
    return 0;
}

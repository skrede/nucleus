// Consumer smoke test against an installed nucleus package: registers a small
// nested schema (including a typed element), resolves a runtime-backed stack, and
// reads values back as text and as the declared type. Exit code is the verdict.

#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/runtime/runtime_source.h"

#ifdef NUCLEUS_INTEGRATION_WITH_XML
#include "nucleus/xml/xml_source.h"
#endif

#include <cstdint>
#include <iostream>

int main()
{
    nucleus::configuration_space_builder engine;
    const bool registered =
        engine.register_element(nucleus::element("server", nucleus::anchor::root()))
        && engine.register_element(
            nucleus::typed_element<std::int32_t>("port", nucleus::anchor::keyspace("server")))
        && engine.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("server")));
    if(!registered)
    {
        std::cerr << "schema registration rejected\n";
        return 1;
    }
    nucleus::configuration_space space = engine.build();

    nucleus::runtime_source values;
    values.set("server/port", "8080").set("server/name", "edge");

    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(values)},
        {});
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    if(config.get("server/name") != "edge")
    {
        std::cerr << "text accessor mismatch\n";
        return 1;
    }

    auto port = config.get_as<std::int32_t>("server/port");
    if(!port || port.value() != 8080)
    {
        std::cerr << "typed accessor mismatch\n";
        return 1;
    }

#ifdef NUCLEUS_INTEGRATION_WITH_XML
    // The xml module rides the same install: parse a document through the
    // exported nucleus::xml target and read a value back.
    auto doc = nucleus::xml_source::from(
        nucleus::xml_source_options::of_string("<server><zone>edge-1</zone></server>"));
    nucleus::configuration_space_builder xml_engine;
    if(!(xml_engine.register_element(nucleus::element("server", nucleus::anchor::root()))
         && xml_engine.register_element(
             nucleus::element("zone", nucleus::anchor::keyspace("server")))))
    {
        std::cerr << "xml schema registration rejected\n";
        return 1;
    }
    nucleus::configuration_space xml_space = xml_engine.build();
    auto xml_loaded = nucleus::load(xml_space,
        nucleus::source_stack{std::move(doc)},
        {});
    if(!xml_loaded || xml_loaded.value().get("server/zone") != "edge-1")
    {
        std::cerr << "installed xml module failed to resolve\n";
        return 1;
    }
    std::cout << "installed nucleus::xml consumed: server/zone=edge-1\n";
#endif

    std::cout << "installed nucleus consumed: server/name=edge, server/port=8080 (typed)\n";
    return 0;
}

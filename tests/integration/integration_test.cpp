// Consumer smoke test against an installed nucleus package: registers a small
// nested schema (including a typed element), resolves a runtime-backed stack, and
// reads values back as text and as the declared type. Exit code is the verdict.

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/detail/format_backend.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/runtime/runtime_source.h"

#ifdef NUCLEUS_INTEGRATION_WITH_XML
#include "nucleus/xml/xml_source.h"
#endif

#include <cstdint>
#include <version>
#include <iostream>

// The one pairing that cannot work: the installed package declares std::format, so its archive
// carries std::format in the interface vocabulary, while this consumer's standard library does
// not offer the type. The reverse pairing is legal and must keep building -- the {fmt} fallback
// lives in a public header and fmt is on the link line.
#if NUCLEUS_USE_STD_FORMAT && !defined(__cpp_lib_format)
#error "the installed nucleus declares std::format, which this standard library does not provide"
#endif

namespace {

nucleus::expected<nucleus::config_space, nucleus::error> make_space()
{
    nucleus::config_space_builder engine;
    if(auto result = engine.register_element(
            nucleus::element("server", nucleus::anchor::root())); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = engine.register_element(
            nucleus::typed_element<std::int32_t>("port", nucleus::anchor::keyspace("server")));
       !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = engine.register_element(
            nucleus::element("name", nucleus::anchor::keyspace("server"))); !result)
        return nucleus::unexpected(std::move(result).error());
    return engine.build();
}

bool read_back(const nucleus::config &config)
{
    if(config.get("server/name") != "edge")
    {
        std::cerr << "text accessor mismatch\n";
        return false;
    }

    auto port = config.get_as<std::int32_t>("server/port");
    if(!port || port.value() != 8080)
    {
        std::cerr << "typed accessor mismatch\n";
        return false;
    }
    return true;
}

bool consume_runtime_stack()
{
    const auto sealed = make_space();
    if(!sealed)
    {
        std::cerr << "schema registration rejected: " << sealed.error() << '\n';
        return false;
    }
    const nucleus::config_space &space = sealed.value();

    nucleus::runtime_source values;
    values.set("server/port", "8080").set("server/name", "edge");

    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(values)},
        {});
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return false;
    }
    return read_back(loaded.value());
}

#ifdef NUCLEUS_INTEGRATION_WITH_XML
// The xml module rides the same install: parse a document through the
// exported nucleus::xml target and read a value back.
nucleus::expected<nucleus::config_space, nucleus::error> make_xml_space()
{
    nucleus::config_space_builder xml_engine;
    if(auto result = xml_engine.register_element(
            nucleus::element("server", nucleus::anchor::root())); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = xml_engine.register_element(
            nucleus::element("zone", nucleus::anchor::keyspace("server"))); !result)
        return nucleus::unexpected(std::move(result).error());
    return xml_engine.build();
}

bool consume_installed_xml()
{
    auto doc = nucleus::xml_source::from(
        nucleus::xml_source_options::of_string("<server><zone>edge-1</zone></server>"));
    const auto sealed = make_xml_space();
    if(!sealed)
    {
        std::cerr << "xml schema registration rejected: " << sealed.error() << '\n';
        return false;
    }
    const nucleus::config_space &xml_space = sealed.value();
    auto xml_loaded = nucleus::load_config(xml_space,
        nucleus::source_stack{std::move(doc)},
        {});
    if(!xml_loaded || xml_loaded.value().get("server/zone") != "edge-1")
    {
        std::cerr << "installed xml module failed to resolve\n";
        return false;
    }
    std::cout << "installed nucleus::xml consumed: server/zone=edge-1\n";
    return true;
}
#endif

}

int main()
{
    if(!consume_runtime_stack())
        return 1;

#ifdef NUCLEUS_INTEGRATION_WITH_XML
    if(!consume_installed_xml())
        return 1;
#endif

    std::cout << "installed nucleus consumed: server/name=edge, server/port=8080 (typed)\n";
    return 0;
}

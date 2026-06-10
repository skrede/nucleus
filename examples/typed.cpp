// typed: register a custom aggregate converter and a built-in int field; read back typed.

#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/xml/xml_source.h"

#include <any>
#include <string>
#include <iostream>
#include <functional>
#include <string_view>

namespace {

struct vec3 { float x = 0.f; float y = 0.f; float z = 0.f; };

// Parses "x,y,z" -- three comma-separated floats. Each component is parsed by
// reusing the built-in float converter rather than re-implementing low-level
// float parsing, so the parse stays locale-independent and portable across
// toolchains (e.g. standard libraries whose <charconv> lacks floating-point
// from_chars).
std::function<nucleus::expected<std::any, std::string>(std::string_view)>
make_vec3_converter()
{
    return [parse_float = nucleus::make_scalar_converter<float>()](
               std::string_view sv) -> nucleus::expected<std::any, std::string> {
        float components[3] = {};
        std::size_t count = 0;
        std::string_view rest = sv;
        while(count < 3 && !rest.empty()) {
            auto sep = rest.find(',');
            std::string_view token = rest.substr(0, sep);
            auto component = parse_float(token);
            if(!component)
                return nucleus::unexpected(std::string("bad component"));
            components[count] = std::any_cast<float>(component.value());
            ++count;
            rest = (sep == std::string_view::npos) ? std::string_view{} : rest.substr(sep + 1);
        }
        if(count != 3 || !rest.empty())
            return nucleus::unexpected(
                std::string("expected x,y,z -- three comma-separated floats"));
        return std::any(vec3{components[0], components[1], components[2]});
    };
}

}

int main()
{
    nucleus::configuration_space_builder builder;
    if(!builder.register_element(nucleus::element("body", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(
        nucleus::typed_element<vec3>("pos", nucleus::anchor::keyspace("body"), make_vec3_converter())))
        return 1;
    if(!builder.register_element(
        nucleus::typed_element<int32_t>("mass", nucleus::anchor::keyspace("body"))))
        return 1;
    nucleus::configuration_space space = builder.build();

    // In-memory document -- no file on disk required.
    const char *document = R"(<body><pos>1.0,2.5,3.0</pos><mass>42</mass></body>)";
    auto make = [document](const std::string &) -> nucleus::source_handle {
        return nucleus::source_handle(
            nucleus::xml_source::from(
                nucleus::xml_source_options::of_string(document)));
    };

    auto loaded = nucleus::load(space, nucleus::source_stack{},
        nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make});
    if(!loaded) {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();

    auto pos = config.get_as<vec3>("body/pos");
    if(!pos) {
        std::cerr << "get_as<vec3> failed: " << pos.error() << '\n';
        return 1;
    }
    std::cout << "pos: " << pos.value().x << ", " << pos.value().y << ", " << pos.value().z << '\n';

    auto mass = config.get_as<int32_t>("body/mass");
    if(!mass) {
        std::cerr << "get_as<int32_t> failed: " << mass.error() << '\n';
        return 1;
    }
    std::cout << "mass: " << mass.value() << '\n';

    // get() and get_as() agree: the text value is still accessible alongside the typed value.
    std::cout << "pos (text): " << config.get("body/pos").value() << '\n';

    // A bad value in the document would fail the resolve, e.g.:
    //   error: conversion failed for 'body/pos': expected x,y,z -- three comma-separated floats (layer: config.xml)
    // get_as with the wrong type produces a type-mismatch error at access:
    //   auto r = config.get_as<int32_t>("body/pos");
    //   // r.error(): "type mismatch for path 'body/pos': stored type does not match requested type"

    return 0;
}

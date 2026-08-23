// typed: register a custom aggregate converter and a built-in int field; read back typed.

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/xml/xml_source.h"

#include <any>
#include <array>
#include <string>
#include <cstdint>
#include <iostream>
#include <functional>
#include <string_view>

namespace {

struct vec3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

using scalar_converter = std::function<
        nucleus::expected<std::any, std::string>(std::string_view)>;

nucleus::expected<float, std::string>
parse_component(const scalar_converter &converter, std::string_view token,
                uint32_t position)
{
    auto converted = converter(token);
    if(!converted)
        return nucleus::unexpected("component " + std::to_string(position) +
                                   ": " + converted.error());
    const float *value = std::any_cast<float>(&converted.value());
    if(value == nullptr)
        return nucleus::unexpected("component parser type mismatch");
    return *value;
}

nucleus::expected<std::any, std::string>
convert_vec3(const scalar_converter &parse_float, std::string_view sv)
{
    const std::size_t first  = sv.find(',');
    const std::size_t second = first == std::string_view::npos
            ? first
            : sv.find(',', first + 1);
    if(second == std::string_view::npos ||
       sv.find(',', second + 1) != std::string_view::npos)
        return nucleus::unexpected(
                std::string("expected x,y,z -- three comma-separated floats"));
    const std::array<std::string_view, 3> tokens{
            sv.substr(0, first), sv.substr(first + 1, second - first - 1),
            sv.substr(second + 1)};
    std::array<float, 3> components{};
    for(uint32_t index = 0; index < components.size(); ++index)
    {
        auto component = parse_component(parse_float, tokens[index], index + 1);
        if(!component)
            return nucleus::unexpected(component.error());
        components[index] = component.value();
    }
    return std::any(vec3{components[0], components[1], components[2]});
}

scalar_converter make_vec3_converter()
{
    return [parse_float = nucleus::make_scalar_converter<float>()](
                   std::string_view sv) -> nucleus::expected<std::any, std::string>
    { return convert_vec3(parse_float, sv); };
}

bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("body", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::typed_element<vec3>("pos", nucleus::anchor::keyspace("body"), make_vec3_converter())) &&
            builder.register_element(
                    nucleus::typed_element<int32_t>("mass", nucleus::anchor::keyspace("body")));
}

// In-memory document -- no file on disk required.
nucleus::source_handle make_document(const std::string &)
{
    const char *document = R"(<body><pos>1.0,2.5,3.0</pos><mass>42</mass></body>)";
    return nucleus::source_handle(
            nucleus::xml_source::from(
                    nucleus::xml_source_options::of_string(document)));
}

// get() and get_as() agree: the text value is still accessible alongside the typed value.
int print_typed_values(const nucleus::config &config)
{
    auto pos = config.get_as<vec3>("body/pos");
    if(!pos)
    {
        std::cerr << "get_as<vec3> failed: " << pos.error() << '\n';
        return 1;
    }
    std::cout << "pos: " << pos.value().x << ", " << pos.value().y << ", " << pos.value().z << '\n';

    auto mass = config.get_as<int32_t>("body/mass");
    if(!mass)
    {
        std::cerr << "get_as<int32_t> failed: " << mass.error() << '\n';
        return 1;
    }
    std::cout << "mass: " << mass.value() << '\n';

    std::cout << "pos (text): " << config.get("body/pos").value() << '\n';

    return 0;
}

// A bad value in the document would fail the resolve, e.g.:
//   error: conversion failed for 'body/pos': expected x,y,z -- three comma-separated floats (layer: config.xml)
// get_as with the wrong type produces a type-mismatch error at access:
//   auto r = config.get_as<int32_t>("body/pos");
//   // r.error(): "type mismatch for path 'body/pos': stored type does not match requested type"

}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    nucleus::config_space space  = builder.build();
    auto                  loaded = nucleus::load_config(space, nucleus::source_stack{},
                                                        nucleus::load_options{.document_paths = {"config.xml"}, .make_document = make_document});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    return print_typed_values(loaded.value());
}

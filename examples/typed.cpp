// typed: register a custom aggregate converter and a built-in int field; read back typed.

#include "nucleus/configuration_space.h"

#include "nucleus/entry/configuration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/xml/xml_source.h"

#include <cmath>
#include <charconv>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include <functional>
#include <string_view>

namespace {

struct vec3 { float x = 0.f; float y = 0.f; float z = 0.f; };

// Parses "x,y,z" -- three comma-separated floats, locale-independent.
std::function<nucleus::result<std::any, std::string>(std::string_view)>
make_vec3_converter()
{
    return [](std::string_view sv) -> nucleus::result<std::any, std::string> {
        float components[3] = {};
        std::size_t count = 0;
        std::string_view rest = sv;
        while(count < 3 && !rest.empty()) {
            auto sep = rest.find(',');
            std::string_view token = rest.substr(0, sep);
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(),
                                             components[count]);
            if(ec != std::errc{} || ptr != token.data() + token.size())
                return nucleus::fail(std::string("bad component"));
            ++count;
            rest = (sep == std::string_view::npos) ? std::string_view{} : rest.substr(sep + 1);
        }
        if(count != 3 || !rest.empty())
            return nucleus::fail(
                std::string("expected x,y,z -- three comma-separated floats"));
        return std::any(vec3{components[0], components[1], components[2]});
    };
}

} // anonymous namespace

int main()
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("body", nucleus::anchor::root()));
    engine.register_element(
        nucleus::typed_element<vec3>("pos", nucleus::anchor::keyspace("body"), make_vec3_converter()));
    engine.register_element(
        nucleus::typed_element<int32_t>("mass", nucleus::anchor::keyspace("body")));

    // In-memory document -- no file on disk required.
    const char *document = R"(<body><pos>1.0,2.5,3.0</pos><mass>42</mass></body>)";
    auto make = [document](const std::string &) -> std::unique_ptr<nucleus::source> {
        return std::make_unique<nucleus::xml::xml_source>(
            nucleus::xml::xml_source::from_string(document));
    };

    auto loaded = engine.load(std::vector<std::string>{"config.xml"}, make);
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

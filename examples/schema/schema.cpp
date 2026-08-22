// schema: the four element kinds, and what the schema rejects at resolve.
//
// element / required_element / identity_element / enum_element declare nodes.
// anchor::root() introduces a top-level node; anchor::keyspace(path) attaches a
// child under an already-declared one. Here the resolve fails: `host` is required
// but no source supplies it.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <string>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("server", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::required_element("host", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                                          {"http", "https"}));
}

static bool is_missing_host(const nucleus::error &error)
{
    return error.code == nucleus::errc::schema_violation &&
            error.message.find("required field 'server/host' is missing") !=
            std::string::npos;
}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    nucleus::config_space space = builder.build();

    nucleus::runtime_source values;
    values.set("server/mode", "http");

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(values)}, {});
    if(loaded)
    {
        std::cerr << "unexpected success\n";
        return 1;
    }
    if(!is_missing_host(loaded.error()))
    {
        std::cerr << "unexpected rejection: " << loaded.error() << '\n';
        return 1;
    }

    std::cout << "rejected as expected: " << loaded.error() << '\n';
    return 0;
}

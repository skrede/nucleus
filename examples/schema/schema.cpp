// schema: the four element kinds, and what the schema rejects at resolve.
//
// element / required_element / identity_element / enum_element declare nodes.
// anchor::root() introduces a top-level node; anchor::keyspace(path) attaches a
// child under an already-declared one. Here the resolve fails: `host` is required
// but no source supplies it.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/env/env_source.h"

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

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    nucleus::config_space space = builder.build();

    // A source that supplies `mode` but not the required `host`.
    nucleus::env_source values;
    values.set("server/mode", "http");

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(values)}, {});
    if(!loaded)
    {
        std::cout << "rejected as expected: " << loaded.error() << '\n';
        return 0;
    }

    std::cerr << "unexpected success\n";
    return 1;
}

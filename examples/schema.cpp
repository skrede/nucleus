// schema: the four element kinds, and what the schema rejects at resolve.
//
// element / required_element / identity_element / enum_element declare nodes.
// anchor::root() introduces a top-level node; anchor::keyspace(path) attaches a
// child under an already-declared one. Here the resolve fails: `host` is required
// but no source supplies it.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/entry/precedence.h"

#include "nucleus/configuration_source/env/env_source.h"

#include <iostream>

int main()
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("server", nucleus::anchor::root()));
    engine.register_element(
        nucleus::required_element("host", nucleus::anchor::keyspace("server")));
    engine.register_element(
        nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                              {"http", "https"}));

    // A source that supplies `mode` but not the required `host`.
    nucleus::env_source values;
    values.set("server/mode", "http");

    nucleus::configuration_source_stack stack;
    stack.add(values, nucleus::layer_rank::base, "config");

    auto loaded = engine.load_configuration(stack);
    if(!loaded)
    {
        std::cout << "rejected as expected: " << loaded.error() << '\n';
        return 0;
    }

    std::cerr << "unexpected success\n";
    return 1;
}

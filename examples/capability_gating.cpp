// capability_gating: load_configuration auto-gates with NO host gate call.
//
// The schema is nested (a `server` container primary-keyed by `name`) and typed
// (an int `port`), so it requires the `nesting` capability. A flat env source
// cannot represent nesting, so the load fails loudly BEFORE folding -- the gate is
// part of every load, not a separate step the host must remember to call.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include <iostream>

int main()
{
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::primary_key_element("name", nucleus::anchor::keyspace("server")));
    builder.register_element(
        nucleus::typed_element<int>("port", nucleus::anchor::keyspace("server")));
    nucleus::configuration_space space = builder.build();

    // A flat env source -- no nesting, no typed scalars.
    nucleus::source_stack_options options;
    options.env = nucleus::env_source_options{{
        {"server/name", "primary"},
    }};

    // No gate/check call anywhere: load_configuration auto-gates on its own.
    auto loaded = nucleus::load_configuration(space, options);
    if(!loaded)
    {
        std::cout << "load auto-gated and refused: " << loaded.error() << '\n';
        return 0;
    }

    std::cerr << "expected the flat source to be refused by the auto-gate\n";
    return 1;
}

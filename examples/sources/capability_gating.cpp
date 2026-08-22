// capability_gating: load auto-gates with NO host gate call.
//
// The schema is nested (a `server` container primary-keyed by `name`) and typed
// (an int `port`), so it requires the `nesting` capability. A flat env source
// cannot represent nesting, so the load fails loudly BEFORE folding -- the gate is
// part of every load, not a separate step the host must remember to call.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/env/env_source.h"

#include <string>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(nucleus::element("server", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::primary_key_element("name", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::typed_element<int>("port", nucleus::anchor::keyspace("server")));
}

static bool is_missing_nesting(const nucleus::error &error)
{
    return error.code == nucleus::errc::unmet_capability &&
            error.message.find(
                    "no source can satisfy capability 'nesting' required by 'schema'") !=
            std::string::npos;
}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    nucleus::config_space space = builder.build();

    nucleus::env_source values;
    values.set("server/name", "primary");

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(values)}, {});
    if(loaded)
    {
        std::cerr << "unexpected success: flat source accepted\n";
        return 1;
    }
    if(!is_missing_nesting(loaded.error()))
    {
        std::cerr << "unexpected rejection: " << loaded.error() << '\n';
        return 1;
    }

    std::cout << "load auto-gated and refused: " << loaded.error() << '\n';
    return 0;
}

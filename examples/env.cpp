// env: a flat (path -> value) source layered into a resolve.
//
// env_source is the simplest source -- the host decides which names map to which
// key paths; the core never reads the process environment itself. Here it is the
// only layer in the stack, resolved without a schema.

#include "nucleus/configuration_space.h"

#include "nucleus/entry/precedence.h"

#include "nucleus/configuration_source/env/env_source.h"

#include <iostream>

int main()
{
    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    // env_source_options carries the host-mapped (path, value) entries by value;
    // it is layered at the env rank by load_configuration.
    nucleus::source_stack_options options;
    options.env = nucleus::env_source_options{{
        {"service/region", "eu-west"},
        {"service/tier", "gold"},
    }};

    auto loaded = nucleus::load_configuration(space, options);
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
    return 0;
}

// env: a flat (path -> value) source layered into a resolve.
//
// env_source is the simplest source -- the host decides which names map to which
// key paths; the core never reads the process environment itself. Here it is the
// only layer in the stack, resolved without a schema.

#include "nucleus/config_space.h"

#include "nucleus/env/env_source.h"

#include <iostream>

int main()
{
    const auto sealed = nucleus::config_space_builder{}.build();
    if(!sealed)
        return 1;
    const nucleus::config_space &space = sealed.value();

    // An env_source carrying the host-mapped (path, value) entries by value.
    nucleus::env_source values;
    values.set("service/region", "eu-west")
          .set("service/tier", "gold");

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(values)}, {});
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::config &config = loaded.value();
    for(const std::string &key : config.keys())
        std::cout << key << " = " << config.get(key).value() << '\n';
    return 0;
}

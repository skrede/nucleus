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
    nucleus::env_source values;
    values.set("service/region", "eu-west")
          .set("service/tier", "gold");

    nucleus::configuration_source_stack stack;
    stack.add(values, nucleus::layer_rank::env, "env");

    nucleus::configuration_space engine;
    auto loaded = engine.load_configuration(stack);
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

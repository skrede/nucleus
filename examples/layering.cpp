// layering: many sources, explicit precedence, and "why is this value X?".
//
// Each layer carries a rank; higher ranks win. After the resolve, provenance_of
// reports which layer supplied each surviving value -- here the env layer's
// `region` survives, while argv overrides `tier`.

#include "nucleus/configuration_space.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/keyspace/provenance.h"

#include "nucleus/configuration_source/env/env_source.h"
#include "nucleus/configuration_source/argv/argv_source.h"

#include <vector>
#include <iostream>

int main()
{
    nucleus::env_source env;
    env.set("service/region", "eu-west").set("service/tier", "silver");

    nucleus::argv_source argv(std::vector<std::string>{"--service-tier=gold"});

    nucleus::configuration_source_stack stack;
    stack.add(env, nucleus::layer_rank::env, "env");
    stack.add(argv, nucleus::layer_rank::argv, "argv");

    nucleus::configuration_space engine;
    auto loaded = engine.load_configuration(stack);
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    for(const std::string &key : config.keys())
    {
        const nucleus::origin *from = config.provenance_of(key);
        std::cout << key << " = " << config.get(key).value()
                  << "  (from " << (from ? from->layer : "?") << ")\n";
    }
    return 0;
}

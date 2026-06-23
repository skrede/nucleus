// layering: many sources, explicit precedence, and "why is this value X?".
//
// Each layer carries a rank; higher ranks win. After the resolve, provenance_of
// reports which layer supplied each surviving value -- here the env layer's
// `region` survives, while argv overrides `tier`.

#include "nucleus/config_space.h"

#include "nucleus/keyspace/provenance.h"

#include "nucleus/env/env_source.h"
#include "nucleus/argv/argv_source.h"

#include <vector>
#include <iostream>

int main()
{
    nucleus::env_source env;
    env.set("service/region", "eu-west").set("service/tier", "silver");

    nucleus::argv_source argv(std::vector<std::string>{"--service-tier=gold"});

    nucleus::config_space space = nucleus::config_space_builder{}.build();

    // env at lower precedence (stack[0]), argv at higher precedence (stack[1]).
    auto loaded = nucleus::load_config(space,
        nucleus::source_stack{std::move(env), std::move(argv)},
        {});
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::config &config = loaded.value();
    for(const std::string &key : config.keys())
    {
        const nucleus::origin *from = config.provenance_of(key);
        std::cout << key << " = " << config.get(key).value()
                  << "  (from " << (from ? from->layer : "?") << ")\n";
    }
    return 0;
}

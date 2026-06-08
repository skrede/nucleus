// tokens: values carrying `${...}` are expanded at load.
//
// The generic core tokenizers (env, string, ...) are installed on every
// configuration_space automatically. Expansion recurses to a fixpoint, so a
// token nested inside another resolves inner-first.

#include "nucleus/configuration_space.h"

#include "nucleus/entry/precedence.h"

#include "nucleus/configuration_source/env/env_source.h"

#include <cstdlib>
#include <iostream>

namespace {

void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

}

int main()
{
    set_env("NUCLEUS_REGION", "eu-west");

    nucleus::env_source values;
    values.set("service/region", "${string.upper(${env.NUCLEUS_REGION})}")
          .set("service/instance", "${string.lower(NODE-${env.NUCLEUS_REGION})}");

    nucleus::configuration_space space = nucleus::configuration_space_builder{}.build();

    nucleus::source_stack_options options;
    options.custom_layers.push_back(nucleus::configuration_source_layer{
        &values, static_cast<std::size_t>(nucleus::layer_rank::base), "config", {}});

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

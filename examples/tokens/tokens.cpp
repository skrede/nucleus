// tokens: values carrying `${...}` are expanded at load.
//
// The generic core tokenizers (env, string, ...) are installed on every
// config_space automatically. Expansion recurses to a fixpoint, so a
// token nested inside another resolves inner-first.

#include "nucleus/config_space.h"

#include "nucleus/env/env_source.h"

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
    values.set("service/region", "${string.upper(value=${env.NUCLEUS_REGION})}")
          .set("service/instance", "${string.lower(value=NODE-${env.NUCLEUS_REGION})}");

    nucleus::config_space space = nucleus::config_space_builder{}.build();

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

#include "nucleus/configuration_space.h"

#include "nucleus/entry/precedence.h"

#include "nucleus/source/env/env_source.h"

#include <cstdlib>
#include <iostream>
#include <string>

// tokens: values carrying `${...}` expressions are expanded at load time.
//
// The generic core tokenizers (env, uuid, string, ...) are installed on every
// configuration_space automatically -- a host registers nothing to get them.
// Expansion runs per-source before layering, recursing to a fixpoint, so a token
// nested inside another resolves inner-first. An unresolvable token fails the
// load loudly rather than passing the raw text through.

namespace {

void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

} // namespace

int main()
{
    set_env("NUCLEUS_REGION", "eu-west");

    // env_source is a flat (path -> text) source. The host decides which names
    // map to which key paths; the core never reads the environment on its own.
    nucleus::env_source values;
    values.set("service/region", "${string.upper(${env.NUCLEUS_REGION})}")
          .set("service/banner", "${string.concat(node-, ${env.NUCLEUS_REGION})}")
          .set("service/instance", "${uuid.v4()}");

    nucleus::source_stack stack;
    stack.add(values, nucleus::layer_rank::base, "config");

    nucleus::configuration_space engine;
    auto loaded = engine.resolve(stack);
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

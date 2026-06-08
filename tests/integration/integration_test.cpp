// Consumer smoke test against an installed nucleus package: registers a small
// schema (including a typed element), resolves an env-backed stack, and reads
// values back as text and as the declared type. Exit code is the verdict.

#include "nucleus/configuration_space.h"

#include "nucleus/entry/precedence.h"
#include "nucleus/entry/configuration.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/converters.h"

#include "nucleus/configuration_source/env/env_source.h"

#include <cstdint>
#include <iostream>

int main()
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("server", nucleus::anchor::root()));
    engine.register_element(
        nucleus::typed_element<std::int32_t>("port", nucleus::anchor::keyspace("server")));
    engine.register_element(
        nucleus::element("name", nucleus::anchor::keyspace("server")));

    nucleus::env_source values;
    values.set("server/port", "8080").set("server/name", "edge");

    nucleus::configuration_source_stack stack;
    stack.add(values, nucleus::layer_rank::base, "base");

    auto loaded = engine.load_configuration(stack);
    if(!loaded)
    {
        std::cerr << "resolve failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    if(config.get("server/name") != "edge")
    {
        std::cerr << "text accessor mismatch\n";
        return 1;
    }

    auto port = config.get_as<std::int32_t>("server/port");
    if(!port || port.value() != 8080)
    {
        std::cerr << "typed accessor mismatch\n";
        return 1;
    }

    std::cout << "installed nucleus consumed: server/name=edge, server/port=8080 (typed)\n";
    return 0;
}

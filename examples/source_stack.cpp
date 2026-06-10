// source_stack: compose exactly the sources you want, in precedence order.
// The last source listed in the stack wins when multiple sources supply the same key.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/configuration.h"

#include "nucleus/env/env_source.h"
#include "nucleus/argv/argv_source.h"
#include "nucleus/runtime/runtime_source.h"

#include <vector>
#include <iostream>

int main()
{
    // Schema: a server container with host, port, and mode.
    nucleus::configuration_space_builder builder;
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(nucleus::element("port", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(
        nucleus::enum_element("mode", nucleus::anchor::keyspace("server"),
                              std::vector<std::string>{"primary", "secondary"})))
        return 1;
    const nucleus::configuration_space space = builder.build();

    // Layer 0 (lowest precedence): programmatic defaults.
    nucleus::runtime_source defaults;
    defaults.set("server/host", "localhost")
            .set("server/port", "8080")
            .set("server/mode", "primary");

    // Layer 1: env-style overrides (e.g. from a deploy environment).
    nucleus::env_source env;
    env.set("server/host", "staging-host")
       .set("server/mode", "secondary");

    // Layer 2 (highest precedence): command-line flags, schema-coupled via recognizer_of.
    nucleus::argv_source argv(std::vector<std::string>{"--server-port=9090"});
    argv.recognize_with(nucleus::recognizer_of(space));

    // Explicit variadic composition: stack order is precedence order, last listed wins.
    auto loaded = nucleus::load(space,
        nucleus::source_stack{std::move(defaults), std::move(env), std::move(argv)},
        {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();

    // Print each resolved value and the source layer that supplied it.
    std::cout << "resolved configuration (last-listed source wins):\n";
    for(const std::string &key : config.keys())
    {
        const nucleus::origin *from = config.provenance_of(key);
        std::cout << "  " << key << " = " << config.get(key).value()
                  << "  (layer rank " << (from ? from->rank : 0) << ")\n";
    }

    // Provenance confirms: env overrode the runtime default for host and mode;
    // argv overrode env and defaults for port; defaults alone supplied nothing higher.
    return 0;
}

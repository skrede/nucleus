// source_stack: compose exactly the sources you want, in precedence order.
// The last source listed in the stack wins when multiple sources supply the same key.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/config.h"

#include "nucleus/env/env_source.h"
#include "nucleus/argv/argv_source.h"
#include "nucleus/runtime/runtime_source.h"

#include <vector>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(
                   nucleus::element("server", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::element("host", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::element("port", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::enum_element(
                            "mode", nucleus::anchor::keyspace("server"),
                            std::vector<std::string>{"primary", "secondary"}));
}

static nucleus::runtime_source make_defaults()
{
    nucleus::runtime_source defaults;
    defaults.set("server/host", "localhost")
            .set("server/port", "8080")
            .set("server/mode", "primary");
    return defaults;
}

static nucleus::env_source make_environment()
{
    nucleus::env_source env;
    env.set("server/host", "staging-host")
            .set("server/mode", "secondary");
    return env;
}

static nucleus::argv_source make_arguments(const nucleus::config_space &space)
{
    nucleus::argv_source argv(std::vector<std::string>{"--server-port=9090"});
    argv.recognize_with(nucleus::recognizer_of(space));
    return argv;
}

// Provenance confirms environment overrides for host and mode and an argv
// override for port, while defaults supply no surviving value.
static void print_resolved(const nucleus::config &config)
{
    std::cout << "resolved config (last-listed source wins):\n";
    for(const std::string &key : config.keys())
    {
        const nucleus::origin *from = config.provenance_of(key);
        std::cout << "  " << key << " = " << config.get(key).value()
                  << "  (layer rank " << (from ? from->rank : 0) << ")\n";
    }
}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const nucleus::config_space space       = builder.build();
    nucleus::runtime_source     defaults    = make_defaults();
    nucleus::env_source         environment = make_environment();
    nucleus::argv_source        arguments   = make_arguments(space);
    auto                        loaded      = nucleus::load_config(
            space,
            nucleus::source_stack{
                    std::move(defaults), std::move(environment), std::move(arguments)},
            {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    print_resolved(loaded.value());
    return 0;
}

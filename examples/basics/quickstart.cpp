// quickstart: declare a schema, resolve a command line against it, read it back.
// Also demonstrates repeated containers: declare a repeated element, load instances,
// navigate via cfg.root()["cluster"]["node"][0]["port"].

#include "nucleus/config_space.h"
#include "nucleus/config_node.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/runtime/runtime_source.h"

#include <vector>
#include <iostream>

static int demo_argv()
{
    nucleus::config_space_builder builder;
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(
               nucleus::element("port", nucleus::anchor::keyspace("server"))))
        return 1;
    nucleus::config_space space = builder.build();

    nucleus::argv_source argv(std::vector<std::string>{"--server-port=8080"});
    argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(argv)}, {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::config &config = loaded.value();
    std::cout << "server/port = " << config.get("server/port").value() << '\n';
    return 0;
}

static bool define_repeated_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(
                   nucleus::element("cluster", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::repeated_element(
                            "node", nucleus::anchor::keyspace("cluster"))) &&
            builder.register_element(
                    nucleus::element(
                            "port", nucleus::anchor::keyspace("cluster/node")));
}

static nucleus::runtime_source make_repeated_source()
{
    nucleus::runtime_source src;
    src.set("cluster/node[0]/port", "80");
    src.set("cluster/node[1]/port", "443");
    return src;
}

static int demo_repeated()
{
    nucleus::config_space_builder builder;
    if(!define_repeated_space(builder))
        return 1;
    nucleus::config_space space  = builder.build();
    auto                  loaded = nucleus::load_config(
            space, nucleus::source_stack{make_repeated_source()}, {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::config &cfg = loaded.value();
    // Navigate via config_node cursor: cluster -> node (repeated) -> [0] -> port.
    auto port0 = cfg.root()["cluster"]["node"][0]["port"].as<std::string>();
    auto port1 = cfg.root()["cluster"]["node"][1]["port"].as<std::string>();
    if(!port0 || !port1)
        return 1;
    std::cout << "cluster/node[0]/port = " << *port0 << '\n';
    std::cout << "cluster/node[1]/port = " << *port1 << '\n';
    return 0;
}

int main()
{
    if(int r = demo_argv(); r != 0)
        return r;
    return demo_repeated();
}

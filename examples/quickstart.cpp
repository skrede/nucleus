// quickstart: declare a schema, resolve a command line against it, read it back.
//
// The schema is the authority: it decides which flags exist. `--server-port` is
// declared, so it resolves; an undeclared flag would fail the load.

#include "nucleus/configuration_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <vector>
#include <iostream>

int main()
{
    nucleus::configuration_space engine;
    engine.register_element(nucleus::element("server", nucleus::anchor::root()));
    engine.register_element(
        nucleus::element("port", nucleus::anchor::keyspace("server")));

    auto loaded = engine.load(std::vector<std::string>{"--server-port=8080"});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    std::cout << "server/port = " << config.get("server/port").value() << '\n';
    return 0;
}

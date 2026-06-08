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
    nucleus::configuration_space_builder builder;
    builder.register_element(nucleus::element("server", nucleus::anchor::root()));
    builder.register_element(
        nucleus::element("port", nucleus::anchor::keyspace("server")));
    nucleus::configuration_space space = builder.build();

    nucleus::source_stack_options options;
    options.argv = nucleus::argv_source_options{{"--server-port=8080"}};
    auto loaded = nucleus::load_configuration(space, options);
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();
    std::cout << "server/port = " << config.get("server/port").value() << '\n';
    return 0;
}

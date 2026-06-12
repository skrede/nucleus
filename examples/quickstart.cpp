// quickstart: declare a schema, resolve a command line against it, read it back.
//
// The schema is the authority: it decides which flags exist. `--server-port` is
// declared, so it resolves; an undeclared flag would fail the load.

#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/argv/argv_source.h"

#include <vector>
#include <iostream>

int main()
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

// argv_recognizer: argv stays coupled to the schema via a host-supplied recognizer.
// It is composed explicitly and never auto-instantiated by load.

#include "nucleus/configuration.h"
#include "nucleus/configuration_space.h"

#include "nucleus/argv/argv_source.h"

#include <vector>
#include <iostream>

int main()
{
    // Schema: two declared key paths the argv source may supply.
    nucleus::configuration_space_builder builder;
    if(!builder.register_schema("server/host"))
        return 1;
    if(!builder.register_schema("server/port"))
        return 1;
    const nucleus::configuration_space space = builder.build();

    // Derive the recognizer from the sealed space. It answers true only for paths
    // the schema declares; everything else is unknown regardless of syntax.
    auto rec = nucleus::recognizer_of(space);

    // Flags that map to declared paths: both are recognized.
    nucleus::argv_source known_argv(std::vector<std::string>{
        "--server-host=edge-node",
        "--server-port=8443",
    });
    known_argv.recognize_with(nucleus::recognizer_of(space));

    auto loaded = nucleus::load(space, nucleus::source_stack{std::move(known_argv)}, {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }

    const nucleus::configuration &config = loaded.value();

    std::cout << "recognized flags (resolved into the configuration):\n";
    for(const std::string &key : config.keys())
        std::cout << "  " << key << " = " << config.get(key).value() << '\n';

    // A flag whose path is not in the schema: strict mode (the default) rejects
    // it at pull() before the fold even starts. The recognizer_of closure is the
    // exact predicate that enforces this boundary.
    nucleus::argv_source unknown_argv(std::vector<std::string>{"--server-timeout=30"});
    unknown_argv.recognize_with(rec)
                .policy(nucleus::unknown_key_policy::strict);

    auto rejected = nucleus::load(space, nucleus::source_stack{std::move(unknown_argv)}, {});
    if(!rejected)
        std::cout << "\nunrecognized flag rejected: " << rejected.error() << '\n';

    return 0;
}

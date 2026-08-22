// argv_recognizer: argv stays coupled to the schema via a host-supplied recognizer.
// It is composed explicitly and never auto-instantiated by load.

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/argv/argv_source.h"

#include <vector>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_schema("server/host") &&
            builder.register_schema("server/port");
}

static nucleus::argv_source make_known_arguments(
        const nucleus::key_recognizer &recognizer)
{
    nucleus::argv_source known_argv(std::vector<std::string>{
            "--server-host=edge-node",
            "--server-port=8443",
    });
    known_argv.recognize_with(recognizer);
    return known_argv;
}

static void print_resolved(const nucleus::config &config)
{
    std::cout << "recognized flags (resolved into the config):\n";
    for(const std::string &key : config.keys())
        std::cout << "  " << key << " = " << config.get(key).value() << '\n';
}

// Strict mode rejects an unknown path at pull() before the fold starts.
static void demonstrate_unknown_rejection(
        const nucleus::config_space   &space,
        const nucleus::key_recognizer &recognizer)
{
    nucleus::argv_source unknown_argv(std::vector<std::string>{"--server-timeout=30"});
    unknown_argv.recognize_with(recognizer)
            .policy(nucleus::unknown_key_policy::strict);
    auto rejected = nucleus::load_config(space, nucleus::source_stack{std::move(unknown_argv)}, {});
    if(!rejected)
        std::cout << "\nunrecognized flag rejected: " << rejected.error() << '\n';
}

int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const nucleus::config_space space = builder.build();
    // The recognizer accepts only schema-declared paths, regardless of syntax.
    const nucleus::key_recognizer recognizer = nucleus::recognizer_of(space);
    nucleus::argv_source          known_argv = make_known_arguments(recognizer);
    auto                          loaded     = nucleus::load_config(space, nucleus::source_stack{std::move(known_argv)}, {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    print_resolved(loaded.value());
    demonstrate_unknown_rejection(space, recognizer);
    return 0;
}

// argv_recognizer: argv stays coupled to the schema via a host-supplied recognizer.
// It is composed explicitly and never auto-instantiated by load.

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/argv/argv_source.h"

#include <string>
#include <vector>
#include <utility>
#include <iostream>

template<typename Builder>
static nucleus::registration_result define_space(Builder &builder)
{
    if(auto result = builder.register_schema("server/host"); !result)
        return result;
    return builder.register_schema("server/port");
}

template<typename Builder>
static nucleus::expected<nucleus::config_space, nucleus::error> make_space(Builder &builder)
{
    if(auto result = define_space(builder); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

static nucleus::expected<nucleus::config_space, nucleus::error> make_space()
{
    nucleus::config_space_builder builder;
    return make_space(builder);
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

static void print_resolved(const nucleus::config &config, std::ostream &output)
{
    output << "recognized flags (resolved into the config):\n";
    for(const std::string &key : config.keys())
        output << "  " << key << " = " << config.get(key).value() << '\n';
}

static bool is_unknown_timeout(const nucleus::error &error)
{
    return error.code == nucleus::errc::schema_violation &&
            error.message.find(
                    "unknown CLI flag '--server-timeout=30' maps to undeclared key 'server/timeout'") !=
            std::string::npos;
}

static int report_unknown_rejection(nucleus::load_result rejected,
                                    std::ostream        &output,
                                    std::ostream        &errors)
{
    if(rejected)
    {
        errors << "unexpected success: unrecognized flag accepted\n";
        return 1;
    }
    if(!is_unknown_timeout(rejected.error()))
    {
        errors << "unexpected rejection: " << rejected.error() << '\n';
        return 1;
    }
    output << "\nunrecognized flag rejected: " << rejected.error() << '\n';
    return 0;
}

// Strict mode rejects an unknown path at pull() before the fold starts.
static int demonstrate_unknown_rejection(
        const nucleus::config_space   &space,
        const nucleus::key_recognizer &recognizer,
        std::ostream                  &output,
        std::ostream                  &errors)
{
    nucleus::argv_source unknown_argv(std::vector<std::string>{"--server-timeout=30"});
    unknown_argv.recognize_with(recognizer)
            .policy(nucleus::unknown_key_policy::strict);
    auto rejected = nucleus::load_config(space, nucleus::source_stack{std::move(unknown_argv)}, {});
    return report_unknown_rejection(std::move(rejected), output, errors);
}

static int run_recognizer_example(nucleus::expected<nucleus::config_space, nucleus::error> space,
                                  std::ostream                                            &output,
                                  std::ostream                                            &errors)
{
    if(!space)
    {
        errors << "space setup failed: " << space.error() << '\n';
        return 1;
    }
    // The recognizer accepts only schema-declared paths, regardless of syntax.
    const nucleus::key_recognizer recognizer = nucleus::recognizer_of(*space);
    nucleus::argv_source          known_argv = make_known_arguments(recognizer);
    auto                          loaded     = nucleus::load_config(
            *space, nucleus::source_stack{std::move(known_argv)}, {});
    if(!loaded)
    {
        errors << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    print_resolved(loaded.value(), output);
    return demonstrate_unknown_rejection(*space, recognizer, output, errors);
}

int main()
{
    return run_recognizer_example(make_space(), std::cout, std::cerr);
}

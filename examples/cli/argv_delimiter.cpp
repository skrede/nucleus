// argv_delimiter: re-delimiting the whole CLI flag grammar at once.
//
// One validated cli_delimiter (here `__`) is handed to every projection of the
// flag bijection -- the source that parses flags, the emitter that renders them,
// and the completion generator -- so the surface a user sees and the surface the
// parser accepts can never drift.

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/argv_emitter.h"

#include "nucleus/completion/completion.h"

#include <vector>
#include <iostream>

static bool define_space(nucleus::config_space_builder &builder)
{
    return builder.register_element(
                   nucleus::element("server", nucleus::anchor::root())) &&
            builder.register_element(
                    nucleus::element("host", nucleus::anchor::keyspace("server"))) &&
            builder.register_element(
                    nucleus::element("port", nucleus::anchor::keyspace("server")));
}

// The source parses `--server__host=...` instead of `--server-host=...`.
static nucleus::argv_source make_arguments(
        const nucleus::config_space  &space,
        const nucleus::cli_delimiter &delimiter)
{
    nucleus::argv_source args(std::vector<std::string>{
            "--server__host=edge-node",
            "--server__port=8443",
    });
    args.delimit_with(delimiter)
            .recognize_with(nucleus::recognizer_of(space));
    return args;
}

static void print_resolved(const nucleus::config &config)
{
    std::cout << "resolved from `__`-delimited flags:\n";
    for(const std::string &key : config.keys())
        std::cout << "  " << key << " = " << config.get(key).value() << '\n';
}

// Every emitted template line uses the same grammar the source accepts.
static bool print_template(
        const nucleus::config_space  &space,
        const nucleus::cli_delimiter &delimiter)
{
    std::cout << "\nflag template under the same delimiter:\n";
    if(auto emitted = nucleus::argv::emit_template(space, std::cout, delimiter); !emitted)
    {
        std::cerr << "emit failed: " << emitted.error() << '\n';
        return false;
    }
    return true;
}

// Completion shares the delimiter, so completed flags match parsed flags.
static void print_completion(
        const nucleus::config_space  &space,
        const nucleus::cli_delimiter &delimiter)
{
    const std::string script =
            space.generate_completion(nucleus::shell::bash, "mytool", delimiter);
    std::cout << "\ncompletion offers `--server__host`: "
              << (script.find("--server__host") != std::string::npos) << '\n';
}

// parse() validates the text: empty, `=`-containing, and `/`-containing
// delimiters (other than `/` itself) are rejected.
int main()
{
    nucleus::config_space_builder builder;
    if(!define_space(builder))
        return 1;
    const nucleus::config_space space = builder.build();
    auto                        delim = nucleus::cli_delimiter::parse("__");
    if(!delim)
    {
        std::cerr << "bad delimiter: " << delim.error() << '\n';
        return 1;
    }
    nucleus::argv_source args   = make_arguments(space, delim.value());
    auto                 loaded = nucleus::load_config(space, nucleus::source_stack{std::move(args)}, {});
    if(!loaded)
    {
        std::cerr << "load failed: " << loaded.error() << '\n';
        return 1;
    }
    print_resolved(loaded.value());
    if(!print_template(space, delim.value()))
        return 1;
    print_completion(space, delim.value());
    return 0;
}

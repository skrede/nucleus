// argv_delimiter: re-delimiting the whole CLI flag grammar at once.
//
// One validated cli_delimiter (here `__`) is handed to every projection of the
// flag bijection -- the source that parses flags, the emitter that renders them,
// and the completion generator -- so the surface a user sees and the surface the
// parser accepts can never drift.

#include "nucleus/configuration_space.h"

#include "nucleus/keyspace/entry.h"
#include "nucleus/keyspace/value.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/argv/argv_source.h"
#include "nucleus/argv/argv_emitter.h"

#include "nucleus/completion/completion.h"

#include <vector>
#include <iostream>

int main()
{
    // Declared elements so the schema projects both flags and template lines.
    nucleus::configuration_space_builder builder;
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(nucleus::element("host", nucleus::anchor::keyspace("server"))))
        return 1;
    if(!builder.register_element(nucleus::element("port", nucleus::anchor::keyspace("server"))))
        return 1;
    const nucleus::configuration_space space = builder.build();

    // parse() validates the text: empty, `=`-containing, and `/`-containing
    // delimiters (other than `/` itself) are rejected.
    auto delim = nucleus::cli_delimiter::parse("__");
    if(!delim)
    {
        std::cerr << "bad delimiter: " << delim.error() << '\n';
        return 1;
    }

    // The source parses `--server__host=...` instead of `--server-host=...`.
    nucleus::argv_source args(std::vector<std::string>{
        "--server__host=edge-node",
        "--server__port=8443",
    });
    args.delimit_with(delim.value())
        .recognize_with(nucleus::recognizer_of(space));

    auto pulled = args.pull();
    if(!pulled)
    {
        std::cerr << "pull failed: " << pulled.error() << '\n';
        return 1;
    }

    std::cout << "mapped from `__`-delimited flags:\n";
    for(const nucleus::keyspace_entry &entry : pulled.value().entries)
        std::cout << "  " << entry.path << " = " << entry.value.text() << '\n';

    // The emitter renders the SAME grammar back: every template line is a flag
    // the source above would accept verbatim.
    std::cout << "\nflag template under the same delimiter:\n";
    nucleus::argv::emit_template(space, std::cout, delim.value());

    // Completion follows the same delimiter, so the completed flags are
    // identical to the parsed ones.
    const std::string script =
        space.generate_completion(nucleus::shell::bash, "mytool", delim.value());
    std::cout << "\ncompletion offers `--server__host`: "
              << (script.find("--server__host") != std::string::npos) << '\n';
    return 0;
}

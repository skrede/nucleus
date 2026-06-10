// completion: project the registered schema into a shell completion script.
//
// nucleus ships no `completion` subcommand -- it exposes the generator and the
// host decides how to surface it. Each element maps to its `--flag` through the
// SAME mapping the CLI uses, and an enum element's value set becomes candidates.

#include "nucleus/configuration_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <iostream>

int main()
{
    nucleus::configuration_space_builder builder;
    if(!builder.register_element(nucleus::element("logging", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(
        nucleus::enum_element("level", nucleus::anchor::keyspace("logging"),
                              {"debug", "info", "warn", "error"})))
        return 1;
    nucleus::configuration_space space = builder.build();

    std::cout << "# ---- bash ----\n";
    std::cout << space.generate_completion(nucleus::shell::bash, "mytool");
    std::cout << "\n# ---- zsh ----\n";
    std::cout << space.generate_completion(nucleus::shell::zsh, "mytool");
    return 0;
}

// completion: project the registered schema into a shell completion script.
//
// nucleus ships no `completion` subcommand -- it exposes the generator and the
// host decides how to surface it. Each element maps to its `--flag` through the
// SAME mapping the CLI uses, and an enum element's value set becomes candidates.

#include "nucleus/config_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <string>
#include <iostream>

using completion_result = nucleus::expected<std::string, nucleus::error>;

static bool print_script(const char *heading, const completion_result &script)
{
    if(!script)
    {
        std::cerr << "completion failed: " << script.error() << '\n';
        return false;
    }
    std::cout << heading << script.value();
    return true;
}

int main()
{
    nucleus::config_space_builder builder;
    if(!builder.register_element(nucleus::element("logging", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(
        nucleus::enum_element("level", nucleus::anchor::keyspace("logging"),
                              {"debug", "info", "warn", "error"})))
        return 1;
    nucleus::config_space space = builder.build();

    const auto bash = space.generate_completion(nucleus::shell::bash, "mytool");
    const auto zsh  = space.generate_completion(nucleus::shell::zsh, "mytool");
    if(!print_script("# ---- bash ----\n", bash))
        return 1;
    return print_script("\n# ---- zsh ----\n", zsh) ? 0 : 1;
}

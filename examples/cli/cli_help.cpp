// help: attach descriptions to schema elements and project them BOTH into a shell
// completion script and a plain --help text.
//
// A description is declared once, with described(...), on the same element that
// carries its value set. It is the single source of truth: the shell completion
// and the --help text below both read that one field, so they never drift.

#include "nucleus/config_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include <iostream>

static bool print_zsh_completion(const nucleus::config_space &space)
{
    const auto script = space.generate_completion(nucleus::shell::zsh, "mytool");
    if(!script)
    {
        std::cerr << "completion failed: " << script.error() << '\n';
        return false;
    }
    std::cout << script.value();
    return true;
}

int main()
{
    nucleus::config_space_builder builder;
    if(!builder.register_element(nucleus::element("logging", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(nucleus::described(
            nucleus::enum_element("level", nucleus::anchor::keyspace("logging"),
                                  {"debug", "info", "warn", "error"}),
            "set the logging level")))
        return 1;
    if(!builder.register_element(nucleus::element("server", nucleus::anchor::root())))
        return 1;
    if(!builder.register_element(nucleus::described(
            nucleus::required_element("host", nucleus::anchor::keyspace("server")),
            "the address the server binds to")))
        return 1;
    nucleus::config_space space = builder.build();

    std::cout << "# ---- --help ----\n";
    std::cout << space.generate_help("mytool");
    std::cout << "\n# ---- zsh completion (descriptions rendered inline) ----\n";
    return print_zsh_completion(space) ? 0 : 1;
}

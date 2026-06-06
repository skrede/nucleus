#include "nucleus/configuration_space.h"

#include "nucleus/completion/completion.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/keyspace/key_path.h"

#include <iostream>
#include <string>

// completion: project a registered schema into a static shell completion script.
//
// nucleus is a library, not a CLI -- it ships no `completion` subcommand. It
// exposes the generator, and the host decides how to surface the result (a hidden
// subcommand, an install hook, a build-time file). Each declared element maps to
// its canonical `--flag` through the SAME inverse mapping the argv surface uses,
// so the completion can never drift from the real CLI, and an element's declared
// value set becomes that flag's completion candidates.

namespace {

nucleus::key_path path_of(const char *text)
{
    return nucleus::key_path::parse(text).value();
}

} // namespace

int main()
{
    nucleus::configuration_space engine;

    engine.register_element(nucleus::element("logging", nucleus::anchor::root()));
    engine.register_element(nucleus::enum_element(
        "level", nucleus::anchor::keyspace(path_of("logging")),
        {"debug", "info", "warn", "error"}));
    engine.register_element(nucleus::element("server", nucleus::anchor::root()));
    engine.register_element(
        nucleus::element("port", nucleus::anchor::keyspace(path_of("server"))));

    std::cout << "# ---- bash ----\n";
    std::cout << engine.generate_completion(nucleus::shell::bash, "mytool");

    std::cout << "\n# ---- zsh ----\n";
    std::cout << engine.generate_completion(nucleus::shell::zsh, "mytool");

    return 0;
}

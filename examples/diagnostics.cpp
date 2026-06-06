// diagnostics: nearest-key suggestions and non-adjudicating conflict reports.
//
// suggest_keys answers "did you mean...?" for a typo'd key. conflicts() surfaces
// two registrations claiming the same path -- naming every claimant and refusing
// to pick a winner, because adjudication is the host's policy, not the core's.

#include "nucleus/configuration_space.h"
#include "nucleus/identity.h"

#include "nucleus/diagnostics/key_suggester.h"
#include "nucleus/diagnostics/conflict_report.h"

#include <vector>
#include <string>
#include <iostream>

int main()
{
    // Nearest-key suggestion over a set of declared keys.
    std::vector<std::string> known{"logging/level", "logging/file", "server/host"};
    for(const std::string &hit : nucleus::suggest_keys("logging/levle", known, 1))
        std::cout << "did you mean: " << hit << '\n';

    // Two owners claim the same path -> one conflict report, no winner chosen.
    nucleus::configuration_space engine;
    engine.register_schema("server/port", nucleus::owner_token(std::string("plugin.a")));
    engine.register_schema("server/port", nucleus::owner_token(std::string("plugin.b")));

    for(const nucleus::conflict_report &report : engine.conflicts())
        std::cout << report.describe() << '\n';
    return 0;
}

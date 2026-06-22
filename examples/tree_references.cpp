// tree_references: demonstrates pass-2 tree-reference resolution.
// Shows abs: (absolute reference), rel: (relative reference),
// the ?? fallback chain, and per-source ${dir.path} location tokens.
// All values are resolved at load time; the configuration is fully self-contained.

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

int main()
{
    // -----------------------------------------------------------------------
    // 1. Absolute reference: ${abs:path/to/field} resolves by root-anchored path.
    //
    // server/port holds the canonical value; app/port_label references it by
    // absolute path so the label is always consistent with the port.
    // -----------------------------------------------------------------------
    {
        auto space = nucleus::config_space_builder{}.build();

        nucleus::runtime_source src;
        src.set("server/port",       "8080");
        src.set("app/port_label",    "Port: ${abs:server/port}");

        auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(src)}, {});
        if(!loaded)
        {
            std::cerr << "abs: example failed: " << loaded.error() << '\n';
            return 1;
        }

        const auto label = loaded.value().get("app/port_label");
        std::cout << "abs: resolved -> " << label.value_or("(absent)") << '\n';
        // Prints: abs: resolved -> Port: 8080
    }

    // -----------------------------------------------------------------------
    // 2. Relative reference: ${rel:./child} descends from the current scope;
    //    ${rel:../sibling} walks up one level then down.
    //
    // server/url references server/host via ${rel:./host} -- the resolver
    // starts at the parent of server/url (i.e. server), then descends to host.
    // -----------------------------------------------------------------------
    {
        auto space = nucleus::config_space_builder{}.build();

        nucleus::runtime_source src;
        src.set("server/host", "localhost");
        src.set("server/url",  "${rel:./host}:8080");

        auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(src)}, {});
        if(!loaded)
        {
            std::cerr << "rel: example failed: " << loaded.error() << '\n';
            return 1;
        }

        const auto url = loaded.value().get("server/url");
        std::cout << "rel: resolved -> " << url.value_or("(absent)") << '\n';
        // Prints: rel: resolved -> localhost:8080
    }

    // -----------------------------------------------------------------------
    // 3. Fallback chain (??): arms are tried left-to-right; the first present
    //    value wins. A quoted literal is the floor when all references are absent.
    //
    // app/timeout tries server/timeout first; because it is absent, the literal
    // "30" is used.
    // -----------------------------------------------------------------------
    {
        auto space = nucleus::config_space_builder{}.build();

        nucleus::runtime_source src;
        // server/timeout is intentionally absent to exercise the fallback.
        src.set("app/timeout", "${abs:server/timeout ?? \"30\"}");

        auto loaded = nucleus::load_config(space, nucleus::source_stack{std::move(src)}, {});
        if(!loaded)
        {
            std::cerr << "?? example failed: " << loaded.error() << '\n';
            return 1;
        }

        const auto timeout = loaded.value().get("app/timeout");
        std::cout << "?? fallback  -> " << timeout.value_or("(absent)") << '\n';
        // Prints: ?? fallback  -> 30
    }

    // -----------------------------------------------------------------------
    // 4. Per-source ${dir.path}: in a multi-file load driven via document_paths,
    //    each document's ${dir.path} token resolves to THAT file's own directory.
    //    This is LOC-01. The demonstration is in location_token_wiring_test.cpp
    //    where two temp files in different directories prove the per-frame binding.
    //    Shown here as a comment rather than live I/O to keep the example self-contained.
    //
    //    Example schema (conceptual):
    //      primary.xml (in /etc/app/):   <config><origin>${dir.path}</origin></config>
    //      base.xml    (in /usr/share/): <config><shared>${dir.path}</shared></config>
    //
    //    After load:
    //      cfg.get("origin")  == "/etc/app"
    //      cfg.get("shared")  == "/usr/share"
    // -----------------------------------------------------------------------
    std::cout << "dir.path       -> (per-source: see location_token_wiring_test)\n";

    return 0;
}

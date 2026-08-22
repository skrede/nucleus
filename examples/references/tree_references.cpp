// tree_references: demonstrates pass-2 tree-reference resolution.
// Shows abs: (absolute reference), rel: (relative reference),
// the ?? fallback chain, and per-source ${dir.path} location tokens.
// All values are resolved at load time; the configuration is fully self-contained.

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

// ${abs:path/to/field} resolves by root-anchored path.
// server/port holds the canonical value; app/port_label references it by
// absolute path so the label is always consistent with the port.
static int demonstrate_absolute_reference()
{
    const auto              space = nucleus::config_space_builder{}.build();
    nucleus::runtime_source source;
    source.set("server/port", "8080");
    source.set("app/port_label", "Port: ${abs:server/port}");
    const auto loaded = nucleus::load_config(
            space, nucleus::source_stack{std::move(source)}, {});
    if(!loaded)
    {
        std::cerr << "abs: example failed: " << loaded.error() << '\n';
        return 1;
    }
    const auto label = loaded.value().get("app/port_label");
    std::cout << "abs: resolved -> " << label.value_or("(absent)") << '\n';
    // Prints: abs: resolved -> Port: 8080
    return 0;
}

// ${rel:./child} descends from the current scope;
// ${rel:../sibling} walks up one level then down.
// server/url references server/host via ${rel:./host} -- the resolver
// starts at the parent of server/url (i.e. server), then descends to host.
static int demonstrate_relative_reference()
{
    const auto              space = nucleus::config_space_builder{}.build();
    nucleus::runtime_source source;
    source.set("server/host", "localhost");
    source.set("server/url", "${rel:./host}:8080");
    const auto loaded = nucleus::load_config(
            space, nucleus::source_stack{std::move(source)}, {});
    if(!loaded)
    {
        std::cerr << "rel: example failed: " << loaded.error() << '\n';
        return 1;
    }
    const auto url = loaded.value().get("server/url");
    std::cout << "rel: resolved -> " << url.value_or("(absent)") << '\n';
    // Prints: rel: resolved -> localhost:8080
    return 0;
}

// Fallback chain (??): arms are tried left-to-right; the first present
// value wins. A quoted literal is the floor when all references are absent.
// app/timeout tries server/timeout first; because it is absent, the literal
// "30" is used.
static int demonstrate_fallback_reference()
{
    const auto              space = nucleus::config_space_builder{}.build();
    nucleus::runtime_source source;
    source.set("app/timeout", "${abs:server/timeout ?? \"30\"}");
    const auto loaded = nucleus::load_config(
            space, nucleus::source_stack{std::move(source)}, {});
    if(!loaded)
    {
        std::cerr << "?? example failed: " << loaded.error() << '\n';
        return 1;
    }
    const auto timeout = loaded.value().get("app/timeout");
    std::cout << "?? fallback  -> " << timeout.value_or("(absent)") << '\n';
    // Prints: ?? fallback  -> 30
    return 0;
}

// Per-source ${dir.path}: in a multi-file load driven via document_paths,
// each document's ${dir.path} token resolves to THAT file's own directory.
// The demonstration is in location_token_wiring_test.cpp
// where two temp files in different directories prove the per-frame binding.
// Shown here as a comment rather than live I/O to keep the example self-contained.
//
// Example schema (conceptual):
//   primary.xml (in /etc/app/):   <config><origin>${dir.path}</origin></config>
//   base.xml    (in /usr/share/): <config><shared>${dir.path}</shared></config>
//
// After load:
//   cfg.get("origin")  == "/etc/app"
//   cfg.get("shared")  == "/usr/share"
static void show_location_token_note()
{
    std::cout << "dir.path       -> (per-source: see location_token_wiring_test)\n";
}

int main()
{
    if(const int status = demonstrate_absolute_reference())
        return status;
    if(const int status = demonstrate_relative_reference())
        return status;
    if(const int status = demonstrate_fallback_reference())
        return status;
    show_location_token_note();
    return 0;
}

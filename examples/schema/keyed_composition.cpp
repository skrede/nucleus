// keyed_composition: cross-layer merge modes on a repeated/identified collection.
//
// Covers: wholesale_replace (default), unite (strict-additive union), replace_by_key
// (matching identifier replaces the whole element), and the loud duplicate-across-layers
// error. Two stacked layers (the second overrides). Domain-neutral (endpoints/output[name]).

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

using namespace nucleus;

// endpoints/output (repeated, `mode`) keyed by `name` via an identity group.
static config_space make_space(merge_mode mode, bool with_identity)
{
    config_space_builder b;
    b.register_element(element("endpoints", anchor::root()));
    b.register_element(
        merging(repeated_element("output", anchor::keyspace("endpoints")), mode));
    b.register_element(element("name", anchor::keyspace("endpoints/output")));
    b.register_element(element("addr", anchor::keyspace("endpoints/output")));
    if(with_identity)
        b.register_identity_group(
            identity_group("output_names", anchor::keyspace("endpoints"))
                .members({"output"}).field("name"));
    return std::move(b).build();
}

static void show(const char *title, merge_mode mode, bool with_identity,
                 runtime_source base, runtime_source over)
{
    std::cout << "--- " << title << " ---\n";
    const config_space space = make_space(mode, with_identity);
    auto r = load_config(space, source_stack{std::move(base), std::move(over)}, {});
    if(!r)
    {
        std::cout << "  REJECTED: " << r.error().message << "\n\n";
        return;
    }
    std::cout << "  outputs: ";
    for(const std::string &n : r->get_all("endpoints/output/name"))
        std::cout << n << " ";
    std::cout << "\n\n";
}

int main()
{
    auto base = [] { runtime_source s;
        s.set("endpoints/output[0]/name", "a").set("endpoints/output[0]/addr", "base-a")
         .set("endpoints/output[1]/name", "b").set("endpoints/output[1]/addr", "base-b");
        return s; };
    auto over_add = [] { runtime_source s;
        s.set("endpoints/output[0]/name", "c").set("endpoints/output[0]/addr", "over-c");
        return s; };
    auto over_redef_b = [] { runtime_source s;
        s.set("endpoints/output[0]/name", "b").set("endpoints/output[0]/addr", "over-b");
        return s; };
    auto over_dup_a = [] { runtime_source s;
        s.set("endpoints/output[0]/name", "a").set("endpoints/output[0]/addr", "over-a");
        return s; };

    // Default: the override supplies output[0] alone, so {a} is replaced and {b} stays.
    show("wholesale_replace (default): base {a,b} + override {c}",
         merge_mode::wholesale_replace, false, base(), over_add());

    // Unite: layers union -> {a,b,c}.
    show("unite: base {a,b} + override {c}",
         merge_mode::unite, true, base(), over_add());

    // Unite: a duplicate identifier across layers is loud (strict-additive).
    show("unite: base {a,b} + override redefines {a} (loud)",
         merge_mode::unite, true, base(), over_dup_a());

    // Replace-by-key: matching 'b' replaced by the override's whole element -> {a,b,...}.
    show("replace_by_key: base {a,b} + override redefines {b}",
         merge_mode::replace_by_key, true, base(), over_redef_b());

    return 0;
}

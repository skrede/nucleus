// constraint_groups: container-scoped constraint groups and identity groups.
//
// Covers: exclusion_group cardinality (at_most/exactly), when_value activation,
// choice over all_of bundles, a Tier-3 validate_group valve, and an identity_group
// (a uniquely-named member set pooled across element-types).
// Domain-neutral vocabulary (server / cache / auth / pool / worker / gateway).

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <iostream>
#include <string>

using namespace nucleus;

// -----------------------------------------------------------------------
// Schema: server/cache{eager,lru,ttl} with a mutually-exclusive policy and a
// ttl-positive valve; server/auth{cert,key,token} as a TLS-or-token choice; and
// a pool of uniquely-named worker/gateway element-types.
// -----------------------------------------------------------------------
static config_space make_space()
{
    config_space_builder b;
    b.register_element(element("server", anchor::root()));

    b.register_element(element("cache", anchor::keyspace("server")));
    b.register_element(element("eager", anchor::keyspace("server/cache")));
    b.register_element(element("lru", anchor::keyspace("server/cache")));
    b.register_element(element("ttl", anchor::keyspace("server/cache")));
    // At most one cache policy active; eager counts only when "true".
    b.register_constraint_group(
        exclusion_group("cache_policy", anchor::keyspace("server/cache"))
            .member("eager", when_value("true"))
            .member("lru")
            .member("ttl")
            .at_most(1));
    // Tier-3 valve: a host rule cardinality cannot express.
    b.register_constraint_group(validate_group(
        "ttl_positive", anchor::keyspace("server/cache"),
        [](const config_node &cache) -> expected<void, std::string> {
            auto ttl = cache["ttl"].value();
            if(ttl.has_value() && *ttl == "0")
                return unexpected(std::string("ttl must be greater than zero"));
            return {};
        }));

    b.register_element(element("auth", anchor::keyspace("server")));
    b.register_element(element("cert", anchor::keyspace("server/auth")));
    b.register_element(element("key", anchor::keyspace("server/auth")));
    b.register_element(element("token", anchor::keyspace("server/auth")));
    // Exactly one auth mode: the {cert,key} bundle or the {token} bundle.
    b.register_constraint_group(
        choice("auth_mode", anchor::keyspace("server/auth"))
            .option(all_of({"cert", "key"}))
            .option(all_of({"token"}))
            .exactly(1));

    b.register_element(element("pool", anchor::keyspace("server")));
    b.register_element(repeated_element("worker", anchor::keyspace("server/pool")));
    b.register_element(element("name", anchor::keyspace("server/pool/worker")));
    b.register_element(repeated_element("gateway", anchor::keyspace("server/pool")));
    b.register_element(element("name", anchor::keyspace("server/pool/gateway")));
    // One namespace: the `name` of every worker/gateway is unique across the pool.
    b.register_identity_group(
        identity_group("component_names", anchor::keyspace("server/pool"))
            .members({"worker", "gateway"}).field("name"));

    return std::move(b).build();
}

static void show(const config_space &space, const char *title, runtime_source src)
{
    std::cout << "--- " << title << " ---\n";
    auto r = load_config(space, source_stack{std::move(src)}, {});
    if(r)
        std::cout << "  OK: configuration is valid\n\n";
    else
        std::cout << "  REJECTED:\n  " << r.error().message << "\n\n";
}

int main()
{
    const config_space space = make_space();

    runtime_source ok;
    ok.set("server/cache/lru", "on")        // exactly one cache policy active
      .set("server/auth/token", "t")        // exactly one auth mode (token bundle)
      .set("server/pool/worker[0]/name", "a")
      .set("server/pool/gateway[0]/name", "b");
    show(space, "valid: one cache policy, one auth mode, unique names", std::move(ok));

    runtime_source two_policies;
    two_policies.set("server/cache/eager", "true").set("server/cache/lru", "on")
                .set("server/auth/token", "t");
    show(space, "violation: two cache policies active", std::move(two_policies));

    runtime_source partial_bundle;
    partial_bundle.set("server/cache/lru", "on").set("server/auth/cert", "c");
    show(space, "violation: partial auth bundle (key missing)", std::move(partial_bundle));

    runtime_source bad_ttl;
    bad_ttl.set("server/cache/ttl", "0").set("server/auth/token", "t");
    show(space, "violation: Tier-3 valve rejects ttl=0", std::move(bad_ttl));

    runtime_source dup;
    dup.set("server/cache/lru", "on").set("server/auth/token", "t")
       .set("server/pool/worker[0]/name", "x")
       .set("server/pool/gateway[0]/name", "x");
    show(space, "violation: duplicate name across worker and gateway", std::move(dup));

    return 0;
}

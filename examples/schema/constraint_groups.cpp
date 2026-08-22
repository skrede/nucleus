// constraint_groups: container-scoped constraint groups and identity groups.
//
// Covers: exclusion_group cardinality (at_most/exactly), when_value activation,
// choice over all_of bundles, a validate_group host valve, and an identity_group
// (a uniquely-named member set pooled across element-types).
// Domain-neutral vocabulary (server / cache / auth / pool / worker / gateway).

#include "nucleus/config_space.h"
#include "nucleus/config.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/constraint_group.h"
#include "nucleus/schema/identity_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <string>
#include <utility>
#include <iostream>

using namespace nucleus;

static registration_result register_cache_elements(config_space_builder &builder)
{
    if(auto result = builder.register_element(element("cache", anchor::keyspace("server"))); !result)
        return result;
    if(auto result = builder.register_element(element("eager", anchor::keyspace("server/cache"))); !result)
        return result;
    if(auto result = builder.register_element(element("lru", anchor::keyspace("server/cache"))); !result)
        return result;
    return builder.register_element(element("ttl", anchor::keyspace("server/cache")));
}

static registration_result register_cache_groups(config_space_builder &builder)
{
    if(auto result = builder.register_constraint_group(
               exclusion_group("cache_policy", anchor::keyspace("server/cache"))
                       .member("eager", when_value("true"))
                       .member("lru")
                       .member("ttl")
                       .at_most(1));
       !result)
        return result;
    // Host-validator valve: a host rule cardinality cannot express.
    return builder.register_constraint_group(validate_group(
            "ttl_positive", anchor::keyspace("server/cache"),
            [](const config_node &cache) -> expected<void, std::string>
            {
                auto ttl = cache["ttl"].value();
                if(ttl.has_value() && *ttl == "0")
                    return nucleus::unexpected(std::string("ttl must be greater than zero"));
                return {};
            }));
}

static registration_result register_cache_constraints(config_space_builder &builder)
{
    if(auto result = register_cache_elements(builder); !result)
        return result;
    return register_cache_groups(builder);
}

static registration_result register_auth_constraints(config_space_builder &builder)
{
    if(auto result = builder.register_element(element("auth", anchor::keyspace("server"))); !result)
        return result;
    if(auto result = builder.register_element(element("cert", anchor::keyspace("server/auth"))); !result)
        return result;
    if(auto result = builder.register_element(element("key", anchor::keyspace("server/auth"))); !result)
        return result;
    if(auto result = builder.register_element(element("token", anchor::keyspace("server/auth"))); !result)
        return result;
    return builder.register_constraint_group(
            choice("auth_mode", anchor::keyspace("server/auth"))
                    .option(all_of({"cert", "key"}))
                    .option(all_of({"token"}))
                    .exactly(1));
}

static registration_result register_pool_identity(config_space_builder &builder)
{
    if(auto result = builder.register_element(element("pool", anchor::keyspace("server"))); !result)
        return result;
    if(auto result = builder.register_element(repeated_element("worker", anchor::keyspace("server/pool"))); !result)
        return result;
    if(auto result = builder.register_element(element("name", anchor::keyspace("server/pool/worker"))); !result)
        return result;
    if(auto result = builder.register_element(repeated_element("gateway", anchor::keyspace("server/pool"))); !result)
        return result;
    if(auto result = builder.register_element(element("name", anchor::keyspace("server/pool/gateway"))); !result)
        return result;
    // One namespace: the `name` of every worker/gateway is unique across the pool.
    return builder.register_identity_group(
            identity_group("component_names", anchor::keyspace("server/pool"))
                    .members({"worker", "gateway"})
                    .field("name"));
}

static expected<config_space, error> make_space()
{
    config_space_builder builder;
    if(auto result = builder.register_element(element("server", anchor::root())); !result)
        return unexpected(std::move(result).error());
    if(auto result = register_cache_constraints(builder); !result)
        return unexpected(std::move(result).error());
    if(auto result = register_auth_constraints(builder); !result)
        return unexpected(std::move(result).error());
    if(auto result = register_pool_identity(builder); !result)
        return unexpected(std::move(result).error());
    return builder.build();
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

static runtime_source make_valid_source()
{
    runtime_source source;
    source.set("server/cache/lru", "on")   // exactly one cache policy active
            .set("server/auth/token", "t") // exactly one auth mode (token bundle)
            .set("server/pool/worker[0]/name", "a")
            .set("server/pool/gateway[0]/name", "b");
    return source;
}

static runtime_source make_two_policy_source()
{
    runtime_source source;
    source.set("server/cache/eager", "true").set("server/cache/lru", "on").set("server/auth/token", "t");
    return source;
}

static runtime_source make_partial_auth_source()
{
    runtime_source source;
    source.set("server/cache/lru", "on").set("server/auth/cert", "c");
    return source;
}

static runtime_source make_bad_ttl_source()
{
    runtime_source source;
    source.set("server/cache/ttl", "0").set("server/auth/token", "t");
    return source;
}

static runtime_source make_duplicate_identity_source()
{
    runtime_source source;
    source.set("server/cache/lru", "on").set("server/auth/token", "t").set("server/pool/worker[0]/name", "x").set("server/pool/gateway[0]/name", "x");
    return source;
}

int main()
{
    auto space = make_space();
    if(!space)
    {
        std::cerr << "space setup failed: " << space.error() << '\n';
        return 1;
    }
    show(*space, "valid: one cache policy, one auth mode, unique names", make_valid_source());
    show(*space, "violation: two cache policies active", make_two_policy_source());
    show(*space, "violation: partial auth bundle (key missing)", make_partial_auth_source());
    show(*space, "violation: host-validator valve rejects ttl=0", make_bad_ttl_source());
    show(*space, "violation: duplicate name across worker and gateway", make_duplicate_identity_source());
    return 0;
}

#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/constraint_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <string>
#include <utility>
#include <iostream>

using namespace nucleus;

template<typename Builder>
static registration_result register_cache_elements(Builder &builder)
{
    if(auto result = builder.register_element(element("cache", anchor::keyspace("server"))); !result)
        return result;
    if(auto result = builder.register_element(element("eager", anchor::keyspace("server/cache"))); !result)
        return result;
    if(auto result = builder.register_element(element("lru", anchor::keyspace("server/cache"))); !result)
        return result;
    return builder.register_element(element("ttl", anchor::keyspace("server/cache")));
}

template<typename Builder>
static registration_result register_cache_groups(Builder &builder)
{
    if(auto result = builder.register_constraint_group(
               exclusion_group("cache_policy", anchor::keyspace("server/cache"))
                       .member("eager", when_value("true"))
                       .member("lru")
                       .member("ttl")
                       .at_most(1));
       !result)
        return result;
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

template<typename Builder>
static registration_result register_cache_constraints(Builder &builder)
{
    if(auto result = register_cache_elements(builder); !result)
        return result;
    return register_cache_groups(builder);
}

template<typename Builder>
static registration_result register_auth_constraints(Builder &builder)
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

template<typename Builder>
static registration_result register_pool_identity(Builder &builder)
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
    return builder.register_identity_group(
            identity_group("component_names", anchor::keyspace("server/pool"))
                    .members({"worker", "gateway"})
                    .field("name"));
}

template<typename Builder>
static expected<config_space, error> make_space(Builder &builder)
{
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

static expected<config_space, error> make_space()
{
    config_space_builder builder;
    return make_space(builder);
}

static void show(const config_space &space, const char *title,
                 runtime_source src, std::ostream &output)
{
    output << "--- " << title << " ---\n";
    auto r = load_config(space, source_stack{std::move(src)}, {});
    if(r)
        output << "  OK: configuration is valid\n\n";
    else
        output << "  REJECTED:\n  " << r.error().message << "\n\n";
}

static runtime_source make_valid_source()
{
    runtime_source source;
    source.set("server/cache/lru", "on")
            .set("server/auth/token", "t")
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

static int run_constraint_groups(expected<config_space, error> space,
                                 std::ostream &output, std::ostream &errors)
{
    if(!space)
    {
        errors << "space setup failed: " << space.error() << '\n';
        return 1;
    }
    show(*space, "valid: one cache policy, one auth mode, unique names", make_valid_source(), output);
    show(*space, "violation: two cache policies active", make_two_policy_source(), output);
    show(*space, "violation: partial auth bundle (key missing)", make_partial_auth_source(), output);
    show(*space, "violation: host-validator valve rejects ttl=0", make_bad_ttl_source(), output);
    show(*space, "violation: duplicate name across worker and gateway", make_duplicate_identity_source(), output);
    return 0;
}

int main()
{
    return run_constraint_groups(make_space(), std::cout, std::cerr);
}

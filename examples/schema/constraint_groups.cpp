#include "nucleus/config.h"
#include "nucleus/config_space.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/identity_group.h"
#include "nucleus/schema/constraint_group.h"

#include "nucleus/runtime/runtime_source.h"

#include <array>
#include <string>
#include <cstdint>
#include <utility>
#include <charconv>
#include <iostream>
#include <string_view>
#include <initializer_list>

using namespace nucleus;

struct expected_outcome
{
    bool             success;
    errc             code;
    std::string_view cue;
};

struct constraint_scenario
{
    std::string_view title;
    runtime_source   source;
    expected_outcome expected;
};

static expected<void, std::string> validate_positive_ttl(const config_node &cache)
{
    const auto ttl = cache["ttl"].value();
    if(!ttl.has_value())
        return {};
    std::int32_t value  = 0;
    const auto   parsed = std::from_chars(ttl->data(), ttl->data() + ttl->size(), value);
    if(parsed.ec != std::errc{} || parsed.ptr != ttl->data() + ttl->size())
        return unexpected(std::string("ttl must be a base-10 int32 without trailing characters"));
    if(value <= 0)
        return unexpected(std::string("ttl must be greater than zero"));
    return {};
}

template<typename Builder>
static registration_result register_elements(Builder                              &builder,
                                             std::initializer_list<schema_element> elements)
{
    for(const schema_element &entry : elements)
        if(auto result = builder.register_element(entry); !result)
            return result;
    return registration_ok();
}

template<typename Builder>
static registration_result register_cache_elements(Builder &builder)
{
    return register_elements(builder, {element("cache", anchor::keyspace("server")), element("eager", anchor::keyspace("server/cache")), element("lru", anchor::keyspace("server/cache")), element("ttl", anchor::keyspace("server/cache"))});
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
            "ttl_positive", anchor::keyspace("server/cache"), validate_positive_ttl));
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
    if(auto result = register_elements(builder, {element("auth", anchor::keyspace("server")), element("cert", anchor::keyspace("server/auth")), element("key", anchor::keyspace("server/auth")), element("token", anchor::keyspace("server/auth"))}); !result)
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
    if(auto result = register_elements(builder, {element("pool", anchor::keyspace("server")), repeated_element("worker", anchor::keyspace("server/pool")), element("name", anchor::keyspace("server/pool/worker")), repeated_element("gateway", anchor::keyspace("server/pool")), element("name", anchor::keyspace("server/pool/gateway"))}); !result)
        return result;
    return builder.register_identity_group(
            identity_group("component_names", anchor::keyspace("server/pool"))
                    .members({"worker", "gateway"})
                    .field("name"));
}

template<typename Builder>
static expected<config_space, error> make_space(Builder &&builder)
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

static expected<config_space, error> make_space() { return make_space(config_space_builder{}); }

static std::int32_t report_result(load_result result, expected_outcome expected,
                                  std::ostream &output, std::ostream &errors)
{
    const bool matches = (expected.success && result) ||
            (!expected.success && !result && result.error().code == expected.code &&
             result.error().message.find(expected.cue) != std::string::npos);
    if(matches)
    {
        if(result)
            output << "  OK: configuration is valid\n\n";
        else
            output << "  REJECTED:\n  " << result.error().message << "\n\n";
        return 0;
    }
    errors << "expectation mismatch: expected " << (expected.success ? "success" : "rejection") << '\n';
    if(result)
        errors << "actual: success\n";
    else
        errors << "actual error: " << result.error() << '\n';
    return 1;
}

static std::int32_t show(const config_space &space, constraint_scenario &scenario,
                         std::ostream &output, std::ostream &errors)
{
    output << "--- " << scenario.title << " ---\n";
    auto result = load_config(space, source_stack{std::move(scenario.source)}, {});
    return report_result(std::move(result), scenario.expected, output, errors);
}

static runtime_source make_source(
        std::initializer_list<std::pair<std::string, std::string>> values)
{
    runtime_source source;
    for(const auto &value : values)
        source.set(value.first, value.second);
    return source;
}

static std::array<constraint_scenario, 5> make_scenarios()
{
    return {{{"valid: one cache policy, one auth mode, unique names", make_source({{"server/cache/lru", "on"}, {"server/auth/token", "t"}, {"server/pool/worker[0]/name", "a"}, {"server/pool/gateway[0]/name", "b"}}), {true, errc::schema_violation, {}}},
             {"violation: two cache policies active", make_source({{"server/cache/eager", "true"}, {"server/cache/lru", "on"}, {"server/auth/token", "t"}}), {false, errc::schema_violation, "requires at most 1 active member(s) but 2 are active"}},
             {"violation: partial auth bundle (key missing)", make_source({{"server/cache/lru", "on"}, {"server/auth/cert", "c"}}), {false, errc::schema_violation, "is partially present (1 of 2)"}},
             {"violation: host-validator valve rejects ttl=0", make_source({{"server/cache/ttl", "0"}, {"server/auth/token", "t"}}), {false, errc::schema_violation, "ttl must be greater than zero"}},
             {"violation: duplicate name across worker and gateway", make_source({{"server/cache/lru", "on"}, {"server/auth/token", "t"}, {"server/pool/worker[0]/name", "x"}, {"server/pool/gateway[0]/name", "x"}}), {false, errc::schema_violation, "is not unique within the slice"}}}};
}

static std::int32_t run_scenarios(const config_space                 &space,
                                  std::array<constraint_scenario, 5> &scenarios,
                                  std::ostream &output, std::ostream &errors)
{
    for(auto &scenario : scenarios)
        if(const std::int32_t status = show(space, scenario, output, errors); status != 0)
            return status;
    return 0;
}

static std::int32_t run_constraint_groups(expected<config_space, error> space,
                                          std::ostream &output, std::ostream &errors)
{
    if(!space)
    {
        errors << "space setup failed: " << space.error() << '\n';
        return 1;
    }
    auto scenarios = make_scenarios();
    return run_scenarios(*space, scenarios, output, errors);
}

int main() { return run_constraint_groups(make_space(), std::cout, std::cerr); }

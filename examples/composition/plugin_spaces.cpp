#include "nucleus/config.h"
#include "nucleus/identity.h"
#include "nucleus/config_space.h"
#include "nucleus/registration_policy.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"

#include "nucleus/runtime/runtime_source.h"

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <iostream>

namespace {
class admitted_plugins_policy : public nucleus::registration_policy
{
public:
    void admit(nucleus::owner_token who) { m_admitted.push_back(std::move(who)); }

    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        for(const nucleus::owner_token &token : m_admitted)
            if(token == request.owner)
                return nucleus::policy_verdict::accept();
        return nucleus::policy_verdict::reject("registration from an unadmitted plugin");
    }

private:
    std::vector<nucleus::owner_token> m_admitted;
};

template<typename Builder>
nucleus::registration_result register_net(Builder                    &builder,
                                          const nucleus::owner_token &owner)
{
    if(auto r = builder.register_element(
               nucleus::element("net", nucleus::anchor::root()), owner);
       !r)
        return r;
    if(auto r = builder.register_element(
               nucleus::element("listen", nucleus::anchor::keyspace("net")), owner);
       !r)
        return r;
    return builder.register_element(
            nucleus::enum_element("proto", nucleus::anchor::keyspace("net"),
                                  std::vector<std::string>{"tcp", "udp"}),
            owner);
}

template<typename Builder>
nucleus::registration_result register_cache(Builder                    &builder,
                                            const nucleus::owner_token &owner)
{
    if(auto r = builder.register_element(
               nucleus::element("cache", nucleus::anchor::root()), owner);
       !r)
        return r;
    if(auto r = builder.register_element(
               nucleus::element("size_mb", nucleus::anchor::keyspace("cache")), owner);
       !r)
        return r;
    return builder.register_element(
            nucleus::enum_element("policy", nucleus::anchor::keyspace("cache"),
                                  std::vector<std::string>{"lru", "lfu"}),
            owner);
}

std::shared_ptr<admitted_plugins_policy> make_policy(
        const nucleus::owner_token &net_owner,
        const nucleus::owner_token &cache_owner)
{
    auto policy = std::make_shared<admitted_plugins_policy>();
    policy->admit(net_owner);
    policy->admit(cache_owner);
    return policy;
}

template<typename Builder>
nucleus::registration_result register_plugins(
        Builder                    &builder,
        const nucleus::owner_token &net_owner,
        const nucleus::owner_token &cache_owner)
{
    if(auto result = register_net(builder, net_owner); !result)
        return result;
    return register_cache(builder, cache_owner);
}

template<typename Builder>
nucleus::expected<nucleus::config_space, nucleus::error> make_application_space(
        Builder                    &builder,
        const nucleus::owner_token &net_owner,
        const nucleus::owner_token &cache_owner,
        std::ostream               &output)
{
    builder.name("app");
    if(auto result = builder.set_registration_policy(make_policy(net_owner, cache_owner)); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = register_plugins(builder, net_owner, cache_owner); !result)
        return nucleus::unexpected(std::move(result).error());
    const nucleus::owner_token rogue_owner(std::string("rogue"));
    auto                       rogue = builder.register_element(
            nucleus::element("rogue", nucleus::anchor::root()), rogue_owner);
    output << "rogue plugin admitted: " << (rogue ? "yes" : "no")
           << "  (" << (rogue ? "" : rogue.error().message) << ")\n";
    output << "cross-plugin conflicts: " << builder.conflicts().size() << "\n\n";
    return builder.build();
}

nucleus::expected<nucleus::config_space, nucleus::error> make_application_space(
        const nucleus::owner_token &net_owner,
        const nucleus::owner_token &cache_owner)
{
    nucleus::config_space_builder builder;
    return make_application_space(builder, net_owner, cache_owner, std::cout);
}

nucleus::runtime_source make_application_source()
{
    nucleus::runtime_source src;
    src.set("net/listen", "0.0.0.0:8080")
            .set("net/proto", "tcp")
            .set("cache/size_mb", "256")
            .set("cache/policy", "lru");
    return src;
}

void print_application(const nucleus::config_space &app,
                       const nucleus::config &config, std::ostream &output)
{
    output << "application-wide config (space \"" << app.space_name() << "\"):\n";
    for(const std::string &key : config.keys())
        output << "  " << key << " = " << config.get(key).value() << '\n';
}

template<typename Builder>
nucleus::expected<nucleus::config_space, nucleus::error> make_private_space(
        Builder                    &builder,
        const nucleus::owner_token &net_owner)
{
    builder.name("net");
    if(auto result = register_net(builder, net_owner); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

nucleus::expected<nucleus::config_space, nucleus::error> make_private_space(
        const nucleus::owner_token &net_owner)
{
    nucleus::config_space_builder builder;
    return make_private_space(builder, net_owner);
}

int run_application(
        nucleus::expected<nucleus::config_space, nucleus::error> app,
        std::ostream &output, std::ostream &errors)
{
    if(!app)
    {
        errors << "application space setup failed: " << app.error() << '\n';
        return 1;
    }
    auto loaded = nucleus::load_config(
            *app, nucleus::source_stack{make_application_source()}, {});
    if(!loaded)
    {
        errors << "app load failed: " << loaded.error() << '\n';
        return 1;
    }
    print_application(*app, loaded.value(), output);
    return 0;
}

int run_private_space(
        nucleus::expected<nucleus::config_space, nucleus::error> net_private,
        std::ostream &output, std::ostream &errors)
{
    if(!net_private)
    {
        errors << "private space setup failed: " << net_private.error() << '\n';
        return 1;
    }
    output << "\nnet's private space is independent of the app space: "
           << "schema elements = " << net_private->schema_elements().size() << '\n';
    return 0;
}
}

int main()
{
    const nucleus::owner_token net_owner(std::string("net")), cache_owner(std::string("cache"));
    const int                  app_status = run_application(
            make_application_space(net_owner, cache_owner), std::cout, std::cerr);
    if(app_status != 0)
        return app_status;
    return run_private_space(make_private_space(net_owner), std::cout, std::cerr);
}

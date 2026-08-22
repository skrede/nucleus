// plugin_spaces: many plugins, one application-wide space.
//
// A config_space is a single flat namespace -- there is no "space inside a space".
// Composition is instead expressed by KEY PATH: each plugin introduces its own
// top-level prefix and anchors its sub-schema under it via anchor::keyspace(...).
//
// Two seams make this a real plugin model rather than a naming convention:
//   - owner_token  tags every registration with "who registered this".
//   - registration_policy  lets the host admit only known plugins and reject the rest.
//
// The same plugin registration function also seals a private, standalone space the
// plugin alone owns and reads -- the "only it can read its own file" case.

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

// Admits registrations only from plugins the host has explicitly handed a token.
// The seam never sees the key path -- it gates on identity, not namespace -- so
// prefix ownership stays a host convention, while admission is enforced here.
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

// A plugin contributes its schema under its own prefix. The first element introduces
// the prefix as a top-level keyspace; the rest anchor beneath it. Reused verbatim
// for both the shared application space and the plugin's own standalone space.
nucleus::registration_result register_net(nucleus::config_space_builder &builder,
                                          const nucleus::owner_token    &owner)
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

nucleus::registration_result register_cache(nucleus::config_space_builder &builder,
                                            const nucleus::owner_token    &owner)
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

nucleus::registration_result register_plugins(
        nucleus::config_space_builder &builder,
        const nucleus::owner_token    &net_owner,
        const nucleus::owner_token    &cache_owner)
{
    if(auto result = register_net(builder, net_owner); !result)
        return result;
    return register_cache(builder, cache_owner);
}

nucleus::expected<nucleus::config_space, nucleus::error> make_application_space(
        const nucleus::owner_token &net_owner,
        const nucleus::owner_token &cache_owner)
{
    nucleus::config_space_builder builder;
    builder.name("app");
    if(auto result = builder.set_registration_policy(make_policy(net_owner, cache_owner)); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = register_plugins(builder, net_owner, cache_owner); !result)
        return nucleus::unexpected(std::move(result).error());
    const nucleus::owner_token rogue_owner(std::string("rogue"));
    auto                       rogue = builder.register_element(
            nucleus::element("rogue", nucleus::anchor::root()), rogue_owner);
    std::cout << "rogue plugin admitted: " << (rogue ? "yes" : "no")
              << "  (" << (rogue ? "" : rogue.error().message) << ")\n";
    std::cout << "cross-plugin conflicts: " << builder.conflicts().size() << "\n\n";
    return builder.build();
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
                       const nucleus::config       &config)
{
    std::cout << "application-wide config (space \"" << app.space_name() << "\"):\n";
    for(const std::string &key : config.keys())
        std::cout << "  " << key << " = " << config.get(key).value() << '\n';
}

nucleus::expected<nucleus::config_space, nucleus::error> make_private_space(
        const nucleus::owner_token &net_owner)
{
    nucleus::config_space_builder builder;
    builder.name("net");
    if(auto result = register_net(builder, net_owner); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}

// A plugin can also seal its own product without sharing a registry.
int show_private_space(const nucleus::owner_token &net_owner)
{
    auto net_private = make_private_space(net_owner);
    if(!net_private)
    {
        std::cerr << "private space setup failed: " << net_private.error() << '\n';
        return 1;
    }
    std::cout << "\nnet's private space is independent of the app space: "
              << "schema elements = " << net_private->schema_elements().size() << '\n';
    return 0;
}

} // namespace

// An unadmitted plugin is refused before it can claim a key, while admitted
// plugins retain owner-attributed collision reporting.
int main()
{
    const nucleus::owner_token net_owner(std::string("net")), cache_owner(std::string("cache"));
    auto                       app = make_application_space(net_owner, cache_owner);
    if(!app)
    {
        std::cerr << "application space setup failed: " << app.error() << '\n';
        return 1;
    }
    auto loaded = nucleus::load_config(
            *app, nucleus::source_stack{make_application_source()}, {});
    if(!loaded)
    {
        std::cerr << "app load failed: " << loaded.error() << '\n';
        return 1;
    }
    print_application(*app, loaded.value());
    return show_private_space(net_owner);
}

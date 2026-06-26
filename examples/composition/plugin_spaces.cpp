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
                                          const nucleus::owner_token &owner)
{
    if(auto r = builder.register_element(
           nucleus::element("net", nucleus::anchor::root()), owner); !r)
        return r;
    if(auto r = builder.register_element(
           nucleus::element("listen", nucleus::anchor::keyspace("net")), owner); !r)
        return r;
    return builder.register_element(
        nucleus::enum_element("proto", nucleus::anchor::keyspace("net"),
                              std::vector<std::string>{"tcp", "udp"}), owner);
}

nucleus::registration_result register_cache(nucleus::config_space_builder &builder,
                                            const nucleus::owner_token &owner)
{
    if(auto r = builder.register_element(
           nucleus::element("cache", nucleus::anchor::root()), owner); !r)
        return r;
    if(auto r = builder.register_element(
           nucleus::element("size_mb", nucleus::anchor::keyspace("cache")), owner); !r)
        return r;
    return builder.register_element(
        nucleus::enum_element("policy", nucleus::anchor::keyspace("cache"),
                              std::vector<std::string>{"lru", "lfu"}), owner);
}

} // namespace

int main()
{
    const nucleus::owner_token net_owner(std::string("net"));
    const nucleus::owner_token cache_owner(std::string("cache"));

    // The grand, application-wide space: one builder, admitting two plugins.
    nucleus::config_space_builder builder;
    builder.name("app");

    auto policy = std::make_shared<admitted_plugins_policy>();
    policy->admit(net_owner);
    policy->admit(cache_owner);
    builder.set_registration_policy(std::move(policy));

    if(!register_net(builder, net_owner))   { std::cerr << "net plugin rejected\n";   return 1; }
    if(!register_cache(builder, cache_owner)){ std::cerr << "cache plugin rejected\n"; return 1; }

    // An unadmitted plugin is refused at the seam, before it can claim any key.
    const nucleus::owner_token rogue_owner(std::string("rogue"));
    auto rogue = builder.register_element(
        nucleus::element("rogue", nucleus::anchor::root()), rogue_owner);
    std::cout << "rogue plugin admitted: " << (rogue ? "yes" : "no")
              << "  (" << (rogue ? "" : rogue.error().message) << ")\n";

    // Cross-plugin key collisions are reported with owner attribution; none here.
    std::cout << "cross-plugin conflicts: " << builder.conflicts().size() << "\n\n";

    const nucleus::config_space app = builder.build();

    nucleus::runtime_source src;
    src.set("net/listen", "0.0.0.0:8080")
       .set("net/proto", "tcp")
       .set("cache/size_mb", "256")
       .set("cache/policy", "lru");

    auto loaded = nucleus::load_config(app, nucleus::source_stack{std::move(src)}, {});
    if(!loaded)
    {
        std::cerr << "app load failed: " << loaded.error() << '\n';
        return 1;
    }

    std::cout << "application-wide config (space \"" << app.space_name() << "\"):\n";
    for(const std::string &key : loaded.value().keys())
        std::cout << "  " << key << " = " << loaded.value().get(key).value() << '\n';

    // The other case: a plugin seals its OWN space, which only it builds and reads.
    // Same registration function, a separate sealed product -- no shared registry.
    nucleus::config_space_builder private_builder;
    private_builder.name("net");
    if(!register_net(private_builder, net_owner)) { std::cerr << "private net build failed\n"; return 1; }
    const nucleus::config_space net_private = private_builder.build();

    std::cout << "\nnet's private space is independent of the app space: "
              << "schema elements = " << net_private.schema_elements().size() << '\n';

    return 0;
}

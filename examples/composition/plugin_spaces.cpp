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
#include <cstddef>
#include <utility>
#include <iostream>

namespace {
class admitted_plugins_policy : public nucleus::registration_policy
{
public:
    explicit admitted_plugins_policy(std::vector<nucleus::owner_token> admitted)
            : m_admitted(std::move(admitted))
    {
    }

    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        for(const nucleus::owner_token &owner : m_admitted)
            if(request.owner == owner)
                return nucleus::policy_verdict::accept();
        return nucleus::policy_verdict::reject("registration from an unadmitted plugin");
    }

private:
    std::vector<nucleus::owner_token> m_admitted;
};
template<typename Builder>
nucleus::registration_result register_plugin(
        Builder &builder, const nucleus::owner_token &owner,
        const std::string &root, const std::string &value,
        const std::string &choice, std::vector<std::string> allowed)
{
    if(auto result = builder.register_element(
               nucleus::element(root, nucleus::anchor::root()), owner);
       !result)
        return result;
    if(auto result = builder.register_element(
               nucleus::element(value, nucleus::anchor::keyspace(root)), owner);
       !result)
        return result;
    return builder.register_element(
            nucleus::enum_element(choice, nucleus::anchor::keyspace(root),
                                  std::move(allowed)),
            owner);
}
template<typename Builder>
inline nucleus::registration_result register_net(Builder &builder, const nucleus::owner_token &owner)
{
    return register_plugin(builder, owner, "net", "listen", "proto", {"tcp", "udp"});
}
template<typename Builder>
inline nucleus::registration_result register_cache(Builder &builder, const nucleus::owner_token &owner)
{
    return register_plugin(builder, owner, "cache", "size_mb", "policy", {"lru", "lfu"});
}
std::shared_ptr<admitted_plugins_policy> make_policy(const nucleus::owner_token &net_owner, const nucleus::owner_token &cache_owner)
{
    return std::make_shared<admitted_plugins_policy>(std::vector<nucleus::owner_token>{net_owner, cache_owner});
}
nucleus::registration_result validate_application(
        nucleus::registration_result rogue, std::size_t conflicts,
        std::ostream &output)
{
    if(rogue)
        return nucleus::unexpected(nucleus::error{
                nucleus::errc::rejected_registration,
                "rogue plugin registration unexpectedly succeeded"});
    const nucleus::error rejection = std::move(rogue).error();
    if(rejection.code != nucleus::errc::rejected_registration ||
       rejection.message.find("registration from an unadmitted plugin") == std::string::npos)
        return nucleus::unexpected(rejection);
    if(conflicts != 0)
        return nucleus::unexpected(nucleus::error{
                nucleus::errc::rejected_registration,
                "cross-plugin conflicts detected: " + std::to_string(conflicts)});
    output << "rogue plugin admitted: no\ncross-plugin conflicts: 0\n\n";
    return nucleus::registration_ok();
}
template<typename Builder>
nucleus::expected<nucleus::config_space, nucleus::error> make_application_space(
        Builder &builder, const nucleus::owner_token &net_owner,
        const nucleus::owner_token &cache_owner, std::ostream &output)
{
    builder.name("app");
    if(auto result = builder.set_registration_policy(make_policy(net_owner, cache_owner)); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = register_net(builder, net_owner); !result)
        return nucleus::unexpected(std::move(result).error());
    if(auto result = register_cache(builder, cache_owner); !result)
        return nucleus::unexpected(std::move(result).error());
    const nucleus::owner_token rogue_owner;
    auto                       rogue = builder.register_element(
            nucleus::element("rogue", nucleus::anchor::root()), rogue_owner);
    if(auto result = validate_application(
               std::move(rogue), builder.conflicts().size(), output);
       !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}
nucleus::runtime_source make_net_source() { return nucleus::runtime_source({{"net/listen", "0.0.0.0:8080"}, {"net/proto", "tcp"}}); }
template<typename Builder>
nucleus::expected<nucleus::config_space, nucleus::error> make_private_space(
        Builder &builder, const nucleus::owner_token &net_owner)
{
    builder.name("net");
    if(auto result = register_net(builder, net_owner); !result)
        return nucleus::unexpected(std::move(result).error());
    return builder.build();
}
int run_application(nucleus::expected<nucleus::config_space, nucleus::error> app,
                    std::ostream &output, std::ostream &errors)
{
    if(!app)
    {
        errors << "application space setup failed: " << app.error() << '\n';
        return 1;
    }
    nucleus::runtime_source source = make_net_source();
    source.set("cache/size_mb", "256").set("cache/policy", "lru");
    auto loaded = nucleus::load_config(
            *app, nucleus::source_stack{std::move(source)}, {});
    if(!loaded)
    {
        errors << "app load failed: " << loaded.error() << '\n';
        return 1;
    }
    output << "application-wide config (space \"" << app->space_name() << "\"):\n";
    for(const std::string &key : loaded->keys())
        output << "  " << key << " = " << *loaded->get(key) << '\n';
    return 0;
}
bool read_private_value(const nucleus::config &config, const std::string &path,
                        const std::string &expected, std::string &value,
                        std::ostream &errors)
{
    auto observed = config.get(path);
    if(!observed)
    {
        errors << "private value missing: " << path << '\n';
        return false;
    }
    value = std::move(*observed);
    if(value == expected)
        return true;
    errors << "private value mismatch: " << path << " expected '" << expected
           << "' observed '" << value << "'\n";
    return false;
}
int run_private_config(nucleus::expected<nucleus::config, nucleus::error> loaded,
                       std::ostream &output, std::ostream &errors)
{
    if(!loaded)
    {
        errors << "private load failed: " << loaded.error() << '\n';
        return 1;
    }
    std::string listen, proto;
    if(!read_private_value(*loaded, "net/listen", "0.0.0.0:8080", listen, errors) ||
       !read_private_value(*loaded, "net/proto", "tcp", proto, errors))
        return 1;
    output << "private net/listen = " << listen << '\n'
           << "private net/proto = " << proto << '\n';
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
    return run_private_config(
            nucleus::load_config(
                    *net_private, nucleus::source_stack{make_net_source()}, {}),
            output, errors);
}
}
int main()
{
    const nucleus::owner_token    net_owner, cache_owner;
    nucleus::config_space_builder app_builder;
    const int                     status = run_application(
            make_application_space(app_builder, net_owner, cache_owner, std::cout),
            std::cout, std::cerr);
    if(status != 0)
        return status;
    nucleus::config_space_builder private_builder;
    return run_private_space(make_private_space(private_builder, net_owner),
                             std::cout, std::cerr);
}

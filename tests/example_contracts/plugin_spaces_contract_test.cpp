int plugin_spaces_example_main();
#define main plugin_spaces_example_main
#include "../../examples/composition/plugin_spaces.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <sstream>
#include <utility>
#include <string_view>

namespace {
nucleus::error setup_error(std::string_view context, std::size_t ordinal) { return {nucleus::errc::rejected_registration, "injected " + std::string(context) + " setup failure " + std::to_string(ordinal)}; }
nucleus::error policy_error() { return {nucleus::errc::rejected_registration, "injected plugin policy failure"}; }
nucleus::error build_error(std::string_view context) { return {nucleus::errc::rejected_registration, "injected " + std::string(context) + " build failure"}; }
nucleus::error rogue_rejection() { return {nucleus::errc::rejected_registration, "registration from an unadmitted plugin"}; }
struct scripted_builder
{
    std::string m_context;
    std::size_t m_fail_at;
    bool        m_fail_policy, m_fail_build;
    std::size_t m_calls, m_policies, m_builds;

    scripted_builder &name(std::string) { return *this; }

    nucleus::registration_result set_registration_policy(std::shared_ptr<nucleus::registration_policy>)
    {
        ++m_policies;
        if(m_fail_policy)
            return nucleus::unexpected(policy_error());
        return nucleus::registration_ok();
    }

    nucleus::registration_result register_element(
            nucleus::schema_element, const nucleus::owner_token &)
    {
        ++m_calls;
        if(m_calls == m_fail_at)
            return nucleus::unexpected(setup_error(m_context, m_fail_at));
        if(m_calls == 7 && (m_context == "application" || m_context == "conflicts"))
            return nucleus::unexpected(rogue_rejection());
        if(m_calls == 7 && m_context == "rogue success")
            return nucleus::registration_ok();
        if(m_calls == 7 && m_context == "wrong error")
            return nucleus::unexpected(nucleus::error{
                    nucleus::errc::schema_violation, "wrong rejection"});
        if(m_calls == 7 && m_context == "wrong diagnostic")
            return nucleus::unexpected(nucleus::error{
                    nucleus::errc::rejected_registration, "wrong diagnostic"});
        return nucleus::registration_ok();
    }

    nucleus::expected<nucleus::config_space, nucleus::error> build()
    {
        ++m_builds;
        if(m_fail_build)
            return nucleus::unexpected(build_error(m_context));
        return nucleus::config_space_builder{}.build();
    }

    std::vector<nucleus::conflict_report> conflicts() const { return m_context == "conflicts" ? std::vector<nucleus::conflict_report>{nucleus::conflict_report("first"), nucleus::conflict_report("second")} : std::vector<nucleus::conflict_report>{}; }

    std::array<std::size_t, 3> state() const { return {m_calls, m_policies, m_builds}; }
};
scripted_builder make_builder(std::string context, std::size_t fail_at = 0, bool fail_policy = false, bool fail_build = false) { return {std::move(context), fail_at, fail_policy, fail_build, 0, 0, 0}; }
template<typename Define>
void verify_positions(std::string_view context, Define define)
{
    const nucleus::owner_token owner;
    for(std::size_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        scripted_builder builder = make_builder(std::string(context), ordinal);
        const auto       result  = define(builder, owner);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == setup_error(context, ordinal));
        REQUIRE(builder.state() == std::array<std::size_t, 3>{ordinal, 0, 0});
    }
}
template<typename Product, typename Run>
std::array<std::string, 3> observe(Product product, Run run)
{
    std::ostringstream output, errors;
    const int          status = run(std::move(product), output, errors);
    return {std::to_string(status), output.str(), errors.str()};
}
nucleus::expected<nucleus::config, nucleus::error> load_private_values(
        bool include_listen, std::string listen,
        bool include_proto, std::string proto)
{
    std::map<std::string, std::string> values;
    if(include_listen)
        values.emplace("net/listen", std::move(listen));
    if(include_proto)
        values.emplace("net/proto", std::move(proto));
    return nucleus::config::from_values(std::move(values));
}
std::array<std::string, 3> failure_observation(const nucleus::error &failure, std::string prefix) { return {"1", "", std::move(prefix) + nucleus::to_string(failure) + "\n"}; }
struct factory_case
{
    bool                       application;
    std::size_t                fail_at;
    bool                       fail_policy, fail_build;
    nucleus::error             expected;
    std::array<std::size_t, 3> state;
};
void verify_factories()
{
    const std::array<factory_case, 6> cases{
            {{true, 0, true, false, policy_error(), {0, 1, 0}},
             {true, 2, false, false, setup_error("application", 2), {2, 1, 0}},
             {true, 5, false, false, setup_error("application", 5), {5, 1, 0}},
             {true, 0, false, true, build_error("application"), {7, 1, 1}},
             {false, 2, false, false, setup_error("private", 2), {2, 0, 0}},
             {false, 0, false, true, build_error("private"), {3, 0, 1}}}};
    const nucleus::owner_token net, cache;
    for(const factory_case &test : cases)
    {
        scripted_builder builder = make_builder(
                test.application ? "application" : "private",
                test.fail_at, test.fail_policy, test.fail_build);
        std::ostringstream output;

        auto product = test.application
                ? make_application_space(builder, net, cache, output)
                : make_private_space(builder, net);
        REQUIRE_FALSE(product.has_value());
        REQUIRE(product.error() == test.expected);
        REQUIRE(builder.state() == test.state);
    }
}
void verify_identity_policy()
{
    const nucleus::owner_token    net, cache, rogue;
    nucleus::config_space_builder builder;
    REQUIRE(builder.set_registration_policy(make_policy(net, cache)));
    REQUIRE(register_net(builder, net));
    const auto result = builder.register_element(
            nucleus::element("net", nucleus::anchor::root()), rogue);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == rogue_rejection());
    REQUIRE(builder.conflicts().empty());
}
void verify_integrity_contract()
{
    const std::array<std::pair<std::string_view, nucleus::error>, 4> cases{{
            {"rogue success", {nucleus::errc::rejected_registration, "rogue plugin registration unexpectedly succeeded"}},
            {"wrong error", {nucleus::errc::schema_violation, "wrong rejection"}},
            {"wrong diagnostic", {nucleus::errc::rejected_registration, "wrong diagnostic"}},
            {"conflicts", {nucleus::errc::rejected_registration, "cross-plugin conflicts detected: 2"}},
    }};
    const nucleus::owner_token                                       net, cache;
    for(const auto &[context, expected] : cases)
    {
        scripted_builder   builder = make_builder(std::string(context));
        std::ostringstream output;
        const auto         result = make_application_space(builder, net, cache, output);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == expected);
        REQUIRE(builder.state() == std::array<std::size_t, 3>{7, 1, 0});
        REQUIRE(output.str().empty());
    }
    scripted_builder   app = make_builder("application");
    std::ostringstream output;
    REQUIRE(make_application_space(app, net, cache, output));
    REQUIRE(app.state() == std::array<std::size_t, 3>{7, 1, 1});
    REQUIRE(output.str() == "rogue plugin admitted: no\ncross-plugin conflicts: 0\n\n");
}
void verify_private_case(bool listen_set, std::string listen, bool proto_set, std::string proto, const std::string &diagnostic) { REQUIRE(observe(load_private_values(listen_set, std::move(listen), proto_set, std::move(proto)), run_private_config) == std::array<std::string, 3>{"1", "", diagnostic}); }
void verify_private_contract()
{
    const nucleus::error failure{nucleus::errc::unreadable_source, "injected private load failure"};
    REQUIRE(observe(nucleus::unexpected(failure),
                    run_private_config) == failure_observation(failure, "private load failed: "));
    verify_private_case(false, "", true, "tcp", "private value missing: net/listen\n");
    verify_private_case(true, "0.0.0.0:8080", false, "", "private value missing: net/proto\n");
    verify_private_case(true, "127.0.0.1:80", true, "tcp", "private value mismatch: net/listen expected '0.0.0.0:8080' observed '127.0.0.1:80'\n");
    verify_private_case(true, "0.0.0.0:8080", true, "udp", "private value mismatch: net/proto expected 'tcp' observed 'udp'\n");
    const nucleus::owner_token    owner;
    nucleus::config_space_builder builder;
    REQUIRE(observe(make_private_space(builder, owner), run_private_space) == std::array<std::string, 3>{"0", "private net/listen = 0.0.0.0:8080\nprivate net/proto = tcp\n", ""});
}
}
TEST_CASE("plugin setup and terminal failures remain exact", "[policy][example]")
{
    verify_positions("net", register_net<scripted_builder>);
    verify_positions("cache", register_cache<scripted_builder>);
    verify_factories();
    const nucleus::error app_failure = setup_error("application", 4);
    REQUIRE(observe(nucleus::unexpected(app_failure), run_application) == failure_observation(app_failure, "application space setup failed: "));
    const nucleus::error private_failure = setup_error("private", 2);
    REQUIRE(observe(nucleus::unexpected(private_failure), run_private_space) == failure_observation(private_failure, "private space setup failed: "));
    verify_identity_policy();
    verify_integrity_contract();
    verify_private_contract();
}

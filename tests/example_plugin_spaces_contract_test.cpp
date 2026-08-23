#define main plugin_spaces_example_main
#include "../examples/composition/plugin_spaces.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <sstream>
#include <utility>
#include <string_view>

namespace {

nucleus::error plugin_setup_error(std::string_view context,
                                  std::size_t      ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected " + std::string(context) + " setup failure " +
                    std::to_string(ordinal)};
}

nucleus::error plugin_policy_error()
{
    return {nucleus::errc::rejected_registration,
            "injected plugin policy failure"};
}

nucleus::error plugin_build_error(std::string_view context)
{
    return {nucleus::errc::rejected_registration,
            "injected " + std::string(context) + " build failure"};
}

class scripted_plugin_builder
{
public:
    scripted_plugin_builder(std::string context, std::size_t fail_at,
                            bool fail_policy, bool fail_build)
            : m_context(std::move(context))
            , m_fail_at(fail_at)
            , m_fail_policy(fail_policy)
            , m_fail_build(fail_build)
            , m_call_count(0)
            , m_policy_count(0)
            , m_build_count(0)
    {
    }

    scripted_plugin_builder &name(std::string) { return *this; }

    nucleus::registration_result set_registration_policy(
            std::shared_ptr<nucleus::registration_policy>)
    {
        ++m_policy_count;
        if(m_fail_policy)
            return nucleus::unexpected(plugin_policy_error());
        return nucleus::registration_ok();
    }

    nucleus::registration_result register_element(
            nucleus::schema_element, const nucleus::owner_token &)
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(
                    plugin_setup_error(m_context, m_fail_at));
        return nucleus::registration_ok();
    }

    nucleus::expected<nucleus::config_space, nucleus::error> build()
    {
        ++m_build_count;
        if(m_fail_build)
            return nucleus::unexpected(plugin_build_error(m_context));
        return nucleus::config_space_builder{}.build();
    }

    std::vector<nucleus::conflict_report> conflicts() const { return {}; }

    std::size_t call_count() const { return m_call_count; }

    std::size_t policy_count() const { return m_policy_count; }

    std::size_t build_count() const { return m_build_count; }

private:
    std::string m_context;
    std::size_t m_fail_at;
    bool        m_fail_policy;
    bool        m_fail_build;
    std::size_t m_call_count;
    std::size_t m_policy_count;
    std::size_t m_build_count;
};

template<typename Define>
void verify_plugin_positions(std::string_view context, Define define)
{
    const nucleus::owner_token owner{std::string(context)};
    for(std::size_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        DYNAMIC_SECTION(context << " registration " << ordinal)
        {
            scripted_plugin_builder builder(std::string(context), ordinal,
                                            false, false);
            const auto              result = define(builder, owner);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == plugin_setup_error(context, ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

}

TEST_CASE("plugin helpers preserve every first registration failure", "[policy][example]")
{
    verify_plugin_positions("net", [](auto &builder, const auto &owner)
                            { return register_net(builder, owner); });
    verify_plugin_positions("cache", [](auto &builder, const auto &owner)
                            { return register_cache(builder, owner); });
}

TEST_CASE("composed plugin setup stops in either plugin", "[policy][example]")
{
    const nucleus::owner_token net(std::string("net"));
    const nucleus::owner_token cache(std::string("cache"));
    for(const std::size_t ordinal : {2U, 5U})
    {
        scripted_plugin_builder builder("plugins", ordinal, false, false);
        const auto              result = register_plugins(builder, net, cache);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == plugin_setup_error("plugins", ordinal));
        REQUIRE(builder.call_count() == ordinal);
        REQUIRE(builder.build_count() == 0);
    }
}

TEST_CASE("application and private factories preserve every failure", "[policy][example]")
{
    const nucleus::owner_token net(std::string("net"));
    const nucleus::owner_token cache(std::string("cache"));
    std::ostringstream         output;

    scripted_plugin_builder policy("application", 0, true, false);
    const auto              policy_product = make_application_space(policy, net, cache, output);
    REQUIRE_FALSE(policy_product.has_value());
    REQUIRE(policy_product.error() == plugin_policy_error());
    REQUIRE(policy.policy_count() == 1);
    REQUIRE(policy.call_count() == 0);
    REQUIRE(policy.build_count() == 0);
    scripted_plugin_builder setup("application", 4, false, false);
    const auto              setup_product = make_application_space(setup, net, cache, output);
    REQUIRE_FALSE(setup_product.has_value());
    REQUIRE(setup_product.error() == plugin_setup_error("application", 4));
    REQUIRE(setup.policy_count() == 1);
    REQUIRE(setup.build_count() == 0);
    scripted_plugin_builder app_build("application", 0, false, true);
    const auto              app_product = make_application_space(app_build, net, cache, output);
    REQUIRE_FALSE(app_product.has_value());
    REQUIRE(app_product.error() == plugin_build_error("application"));
    REQUIRE(app_build.build_count() == 1);
    scripted_plugin_builder private_setup("private", 2, false, false);
    const auto              private_product = make_private_space(private_setup, net);
    REQUIRE_FALSE(private_product.has_value());
    REQUIRE(private_product.error() == plugin_setup_error("private", 2));
    REQUIRE(private_setup.build_count() == 0);
    scripted_plugin_builder private_build("private", 0, false, true);
    const auto              private_build_product = make_private_space(private_build, net);
    REQUIRE_FALSE(private_build_product.has_value());
    REQUIRE(private_build_product.error() == plugin_build_error("private"));
    REQUIRE(private_build.build_count() == 1);
}

TEST_CASE("plugin terminals stop before load and display work", "[policy][example]")
{
    std::ostringstream output;
    std::ostringstream errors;
    const auto         app_error  = plugin_setup_error("application", 4);
    const int          app_status = run_application(
            nucleus::unexpected(app_error), output, errors);
    REQUIRE(app_status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() == "application space setup failed: " + nucleus::to_string(app_error) + "\n");

    output.str("");
    errors.str("");
    const auto private_error  = plugin_setup_error("private", 2);
    const int  private_status = run_private_space(
            nucleus::unexpected(private_error), output, errors);
    REQUIRE(private_status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() == "private space setup failed: " + nucleus::to_string(private_error) + "\n");
}

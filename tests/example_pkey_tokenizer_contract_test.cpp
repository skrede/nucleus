#define main pkey_tokenizer_example_main
#include "../examples/tokens/pkey_tokenizer.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstddef>
#include <sstream>
#include <utility>
#include <string_view>

namespace {

nucleus::error tokenizer_setup_error(std::string_view context, std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected " + std::string(context) + " setup failure " +
                    std::to_string(ordinal)};
}

nucleus::error tokenizer_install_error()
{
    return {nucleus::errc::rejected_registration,
            "injected host tokenizer install failure"};
}

class scripted_tokenizer_builder
{
public:
    scripted_tokenizer_builder(std::string context, std::size_t fail_at,
                               bool fail_install)
            : m_context(std::move(context))
            , m_fail_at(fail_at)
            , m_fail_install(fail_install)
            , m_call_count(0)
            , m_build_count(0)
            , m_install_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element,
                                                  const nucleus::owner_token & = {})
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(tokenizer_setup_error(m_context, m_fail_at));
        return nucleus::registration_ok();
    }

    nucleus::registration_result install_tree_tokenizer(nucleus::tree_tokenizer,
                                                        nucleus::owner_token = {})
    {
        ++m_install_count;
        if(m_fail_install)
            return nucleus::unexpected(tokenizer_install_error());
        return nucleus::registration_ok();
    }

    nucleus::config_space build()
    {
        ++m_build_count;
        return nucleus::config_space_builder{}.build();
    }

    std::size_t call_count() const { return m_call_count; }

    std::size_t install_count() const { return m_install_count; }

    std::size_t build_count() const { return m_build_count; }

private:
    std::string m_context;
    std::size_t m_fail_at;
    bool        m_fail_install;
    std::size_t m_call_count;
    std::size_t m_build_count;
    std::size_t m_install_count;
};

template<typename Define>
void verify_setup_positions(std::string_view context, Define define)
{
    for(std::size_t ordinal = 1; ordinal <= 5; ++ordinal)
    {
        DYNAMIC_SECTION(context << " registration " << ordinal)
        {
            scripted_tokenizer_builder builder(std::string(context), ordinal, false);
            const auto                 result = define(builder);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == tokenizer_setup_error(context, ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

}

TEST_CASE("built-in tokenizer setup preserves each first registration failure",
          "[pkey_tokenizer][example]")
{
    verify_setup_positions("built-in", [](auto &builder)
                           { return define_space(builder, "description"); });
}
TEST_CASE("host tokenizer setup preserves each first registration failure",
          "[pkey_tokenizer][example]")
{
    verify_setup_positions("host", [](auto &builder)
                           { return define_space(builder, "label"); });
}

TEST_CASE("tokenizer factories preserve setup and install failures",
          "[pkey_tokenizer][example]")
{
    scripted_tokenizer_builder builtin("built-in", 4, false);
    const auto                 builtin_product = make_server_space(builtin);
    REQUIRE_FALSE(builtin_product.has_value());
    REQUIRE(builtin_product.error() == tokenizer_setup_error("built-in", 4));
    REQUIRE(builtin.build_count() == 0);
    scripted_tokenizer_builder host_setup("host", 3, false);
    const auto                 host_product = make_host_space(host_setup);
    REQUIRE_FALSE(host_product.has_value());
    REQUIRE(host_product.error() == tokenizer_setup_error("host", 3));
    REQUIRE(host_setup.install_count() == 0);
    REQUIRE(host_setup.build_count() == 0);
    scripted_tokenizer_builder host_install("host", 0, true);
    const auto                 install_product = make_host_space(host_install);
    REQUIRE_FALSE(install_product.has_value());
    REQUIRE(install_product.error() == tokenizer_install_error());
    REQUIRE(host_install.call_count() == 5);
    REQUIRE(host_install.install_count() == 1);
    REQUIRE(host_install.build_count() == 0);
}

TEST_CASE("tokenizer terminals stop before narrative work on setup failure",
          "[pkey_tokenizer][example]")
{
    std::ostringstream output;
    std::ostringstream errors;
    const auto         builtin_error  = tokenizer_setup_error("built-in", 2);
    const int          builtin_status = run_builtin_tokenizer(
            nucleus::unexpected(builtin_error), output, errors);
    REQUIRE(builtin_status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() == "space setup failed (built-in tokenizer): " + nucleus::to_string(builtin_error) + "\n");
    output.str("");
    errors.str("");
    const auto host_error  = tokenizer_install_error();
    const int  host_status = run_host_tokenizer(
            nucleus::unexpected(host_error), output, errors);
    REQUIRE(host_status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() == "space setup failed (host tokenizer): " + nucleus::to_string(host_error) + "\n");
}

TEST_CASE("tokenizer narratives require exact selected values",
          "[pkey_tokenizer][example]")
{
    std::ostringstream output;
    std::ostringstream errors;
    REQUIRE(run_builtin_tokenizer(make_server_space(), output, errors) == 0);
    REQUIRE(errors.str().empty());
    REQUIRE(output.str() ==
            "primary:   primary at 10.0.0.1:9000\n"
            "secondary: secondary at 10.0.0.2:9000\n");
    output.str("");
    REQUIRE(run_host_tokenizer(make_host_space(), output, errors) == 0);
    REQUIRE(errors.str().empty());
    REQUIRE(output.str() == "host tok:  host: alpha at 10.0.1.1:9000\n");
}

TEST_CASE("host tokenizer matches selected direct-child failures",
          "[pkey_tokenizer][example]")
{
    struct access_case
    {
        bool             selected;
        std::string_view field;
        std::string_view cue;
    };
    const access_case cases[] = {
            {false, "endpoint", "selected primary-key instance"},
            {true, "missing", "has no field 'missing'"},
            {true, "endpoint/secret", "one direct child segment"}};
    const auto container = nucleus::key_path::parse("cluster/server").value();
    const auto current   = nucleus::key_path::parse("cluster/server/label").value();
    const auto tokenizer = make_host_tokenizer();
    for(const access_case &one : cases)
    {
        nucleus::keyspace building;
        if(one.selected)
            building.set(container.child("name"), nucleus::value::owned("alpha"));
        const nucleus::tree_access access{building, current, "server", one.field};
        const auto                 result = tokenizer.resolve(access);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == nucleus::resolve_errc::missing_field);
        REQUIRE(result.error().message.find(one.cue) != std::string::npos);
    }
}

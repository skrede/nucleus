#define main constraint_groups_example_main
#include "../examples/schema/constraint_groups.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstddef>
#include <sstream>
#include <utility>
#include <string_view>

namespace {

nucleus::error constraint_setup_error(std::string_view context,
                                      std::size_t      ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected " + std::string(context) + " setup failure " +
                    std::to_string(ordinal)};
}

class scripted_constraint_builder
{
public:
    scripted_constraint_builder(std::string context, std::size_t fail_at)
            : m_context(std::move(context))
            , m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element)
    {
        return next_result();
    }

    nucleus::registration_result register_constraint_group(
            nucleus::constraint_group)
    {
        return next_result();
    }

    nucleus::registration_result register_identity_group(
            nucleus::identity_group_spec)
    {
        return next_result();
    }

    nucleus::config_space build()
    {
        ++m_build_count;
        return nucleus::config_space_builder{}.build();
    }

    std::size_t call_count() const { return m_call_count; }

    std::size_t build_count() const { return m_build_count; }

private:
    nucleus::registration_result next_result()
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(
                    constraint_setup_error(m_context, m_fail_at));
        return nucleus::registration_ok();
    }

    std::string m_context;
    std::size_t m_fail_at;
    std::size_t m_call_count;
    std::size_t m_build_count;
};

template<typename Define>
void verify_constraint_positions(std::string_view context,
                                 std::size_t count, Define define)
{
    for(std::size_t ordinal = 1; ordinal <= count; ++ordinal)
    {
        DYNAMIC_SECTION(context << " registration " << ordinal)
        {
            scripted_constraint_builder builder(std::string(context), ordinal);
            const auto                  result = define(builder);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == constraint_setup_error(context, ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

}

TEST_CASE("constraint setup preserves every first registration failure",
          "[constraint][example]")
{
    verify_constraint_positions("cache elements", 4,
                                [](auto &builder)
                                { return register_cache_elements(builder); });
    verify_constraint_positions("cache groups", 2,
                                [](auto &builder)
                                { return register_cache_groups(builder); });
    verify_constraint_positions("auth", 5,
                                [](auto &builder)
                                { return register_auth_constraints(builder); });
    verify_constraint_positions("pool identity", 6,
                                [](auto &builder)
                                { return register_pool_identity(builder); });
}

TEST_CASE("composed cache setup stops in the failing helper",
          "[constraint][example]")
{
    for(const std::size_t ordinal : {3U, 5U})
    {
        scripted_constraint_builder builder("cache composite", ordinal);
        const auto                  result = register_cache_constraints(builder);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() ==
                constraint_setup_error("cache composite", ordinal));
        REQUIRE(builder.call_count() == ordinal);
        REQUIRE(builder.build_count() == 0);
    }
}

TEST_CASE("constraint product and terminal preserve setup failure",
          "[constraint][example]")
{
    for(const std::size_t ordinal : {1U, 9U})
    {
        scripted_constraint_builder builder("space", ordinal);
        const auto                  product = make_space(builder);
        REQUIRE_FALSE(product.has_value());
        REQUIRE(product.error() == constraint_setup_error("space", ordinal));
        REQUIRE(builder.call_count() == ordinal);
        REQUIRE(builder.build_count() == 0);
    }

    scripted_constraint_builder builder("space", 9);
    auto                        product = make_space(builder);
    std::ostringstream          output;
    std::ostringstream          errors;
    const int                   status = run_constraint_groups(std::move(product), output, errors);
    REQUIRE(status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() ==
            "space setup failed: rejected_registration: "
            "injected space setup failure 9\n");
}

TEST_CASE("constraint setup retains the real builder path",
          "[constraint][example]")
{
    const auto product = make_space();
    REQUIRE(product.has_value());
    REQUIRE(product->schema_elements().size() == 14);
}

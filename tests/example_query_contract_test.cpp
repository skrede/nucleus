#define main query_example_main
#include "../examples/references/query.cpp"
#undef main

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <cstddef>
#include <sstream>
#include <utility>

namespace {

nucleus::error query_setup_error(std::size_t ordinal)
{
    return {nucleus::errc::rejected_registration,
            "injected query setup failure " + std::to_string(ordinal)};
}

class scripted_query_builder
{
public:
    explicit scripted_query_builder(std::size_t fail_at)
            : m_fail_at(fail_at)
            , m_call_count(0)
            , m_build_count(0)
    {
    }

    nucleus::registration_result register_element(nucleus::schema_element,
                                                  const nucleus::owner_token & = {})
    {
        ++m_call_count;
        if(m_call_count == m_fail_at)
            return nucleus::unexpected(query_setup_error(m_fail_at));
        return nucleus::registration_ok();
    }

    nucleus::config_space build()
    {
        ++m_build_count;
        return nucleus::config_space_builder{}.build();
    }

    std::size_t call_count() const { return m_call_count; }

    std::size_t build_count() const { return m_build_count; }

private:
    std::size_t m_fail_at;
    std::size_t m_call_count;
    std::size_t m_build_count;
};

}

TEST_CASE("query setup preserves each first registration failure", "[selector][example]")
{
    for(std::size_t ordinal = 1; ordinal <= 5; ++ordinal)
    {
        DYNAMIC_SECTION("registration " << ordinal)
        {
            scripted_query_builder builder(ordinal);
            const auto             result = define_server_space(builder);

            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == query_setup_error(ordinal));
            REQUIRE(builder.call_count() == ordinal);
            REQUIRE(builder.build_count() == 0);
        }
    }
}

TEST_CASE("query product and terminal preserve setup failure", "[selector][example]")
{
    scripted_query_builder builder(3);
    auto                   product = make_server_space(builder);

    REQUIRE_FALSE(product.has_value());
    REQUIRE(product.error() == query_setup_error(3));
    REQUIRE(builder.call_count() == 3);
    REQUIRE(builder.build_count() == 0);

    std::ostringstream output;
    std::ostringstream errors;
    const int          status = run_query_example(std::move(product), output, errors);

    REQUIRE(status == 1);
    REQUIRE(output.str().empty());
    REQUIRE(errors.str() ==
            "space setup failed: rejected_registration: injected query setup failure 3\n");
}

TEST_CASE("query setup retains the real builder path", "[selector][example]")
{
    auto product = make_server_space();

    REQUIRE(product.has_value());
    REQUIRE(product->schema_elements().size() == 5);
}

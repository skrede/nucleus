#include "nucleus/nucleus.h"

#include "nucleus/schema/schema_registry.h"
#include "nucleus/source/source_registry.h"
#include "nucleus/registration_policy.h"
#include "nucleus/tokenizer/tokenizer_registry.h"

#include <memory>
#include <utility>

namespace nucleus {

// The facade composition-owns the three flat sibling registries plus the host
// registration policy. This is the single place the registries are owned; they
// hold no references to one another.
class nucleus::impl
{
public:
    registration_result review(registration_kind kind, const owner_token &owner)
    {
        registration_request request{kind, owner};
        policy_verdict verdict = m_policy->review(request);
        if(!verdict.accepted())
            return fail(verdict.reason());
        return registration_ok();
    }

    schema_registry schema;
    tokenizer_registry tokenizer;
    source_registry sources;
    std::shared_ptr<registration_policy> m_policy = std::make_shared<registration_policy>();
};

nucleus::nucleus() : m_impl(std::make_unique<impl>()) {}

nucleus::~nucleus() = default;

nucleus::nucleus(nucleus &&) noexcept = default;

nucleus &nucleus::operator=(nucleus &&) noexcept = default;

void nucleus::set_registration_policy(std::shared_ptr<registration_policy> policy)
{
    m_impl->m_policy = policy ? std::move(policy)
                              : std::make_shared<registration_policy>();
}

registration_result nucleus::register_schema(std::string key_path, owner_token owner)
{
    if(auto verdict = m_impl->review(registration_kind::schema, owner); !verdict)
        return verdict;
    m_impl->schema.add(schema_spec{std::move(key_path)}, std::move(owner));
    return registration_ok();
}

registration_result nucleus::register_tokenizer(std::string name, owner_token owner)
{
    if(auto verdict = m_impl->review(registration_kind::tokenizer, owner); !verdict)
        return verdict;
    m_impl->tokenizer.add(tokenizer_spec{std::move(name)}, std::move(owner));
    return registration_ok();
}

registration_result nucleus::register_source(std::string name, owner_token owner)
{
    if(auto verdict = m_impl->review(registration_kind::source, owner); !verdict)
        return verdict;
    m_impl->sources.add(source_spec{std::move(name)}, std::move(owner));
    return registration_ok();
}

std::size_t nucleus::schema_count() const noexcept { return m_impl->schema.size(); }

std::size_t nucleus::tokenizer_count() const noexcept { return m_impl->tokenizer.size(); }

std::size_t nucleus::source_count() const noexcept { return m_impl->sources.size(); }

}

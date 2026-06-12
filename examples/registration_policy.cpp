// registration_policy: intercept registrations before they commit.
//
// The core imposes no reservation or namespacing rules -- mechanism in core,
// policy in the host. A host installs a registration_policy whose review() can
// reject a registration with a reason the facade surfaces verbatim.

#include "nucleus/config_space.h"
#include "nucleus/registration_policy.h"

#include <memory>
#include <iostream>

namespace {

// A policy that reserves the tokenizer surface for the host: it accepts schema
// registrations but refuses any tokenizer registration.
class reserve_tokenizers final : public nucleus::registration_policy
{
public:
    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        if(request.kind == nucleus::registration_kind::tokenizer)
            return nucleus::policy_verdict::reject("tokenizers are reserved by the host");
        return nucleus::policy_verdict::accept();
    }
};

}

int main()
{
    nucleus::config_space_builder builder;
    if(!builder.set_registration_policy(std::make_shared<reserve_tokenizers>()))
        return 1;

    auto schema = builder.register_schema("logging/level");
    std::cout << "schema registration accepted: " << std::boolalpha
              << static_cast<bool>(schema) << '\n';

    auto tokenizer = builder.register_tokenizer("custom");
    if(!tokenizer)
        std::cout << "tokenizer registration rejected: " << tokenizer.error() << '\n';
    return 0;
}

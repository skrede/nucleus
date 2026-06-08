// registration_policy: intercept registrations before they commit.
//
// The core imposes no reservation or namespacing rules -- mechanism in core,
// policy in the host. A host installs a registration_policy whose review() can
// reject a registration with a reason the facade surfaces verbatim.

#include "nucleus/configuration_space.h"
#include "nucleus/registration_policy.h"

#include <memory>
#include <iostream>

namespace {

// A policy that reserves the source surface for the host: it accepts schema and
// tokenizer registrations but refuses any source registration.
class reserve_sources final : public nucleus::registration_policy
{
public:
    nucleus::policy_verdict review(const nucleus::registration_request &request) override
    {
        if(request.kind == nucleus::registration_kind::configuration_source)
            return nucleus::policy_verdict::reject("sources are reserved by the host");
        return nucleus::policy_verdict::accept();
    }
};

}

int main()
{
    nucleus::configuration_space engine;
    engine.set_registration_policy(std::make_shared<reserve_sources>());

    auto schema = engine.register_schema("logging/level");
    std::cout << "schema registration accepted: " << std::boolalpha
              << static_cast<bool>(schema) << '\n';

    auto source = engine.register_source("argv");
    if(!source)
        std::cout << "source registration rejected: " << source.error() << '\n';
    return 0;
}

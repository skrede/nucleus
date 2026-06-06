#include "nucleus/host/host_tokenizer.h"

#include "nucleus/host/host_platform.h"

#include "nucleus/tokenizer/tokenizer_builder.h"
#include "nucleus/tokenizer/resolve_error.h"

#include "nucleus/format.h"

#include <string>
#include <utility>
#include <functional>

namespace nucleus {

namespace {

// Wraps a platform fact getter into a field resolver: a non-empty value
// resolves, an empty value (the platform could not answer) is missing_field.
field_resolver host_field(const char *name, std::string (*getter)())
{
    std::string field_name(name);
    return [field_name, getter]() -> token_result {
        std::string value = getter();
        if(value.empty())
            return fail(resolve_error(resolve_errc::missing_field,
                                      nucleus::format("HOST.{} is unavailable on this platform",
                                                      field_name)));
        return value;
    };
}

}

tokenizer make_host_tokenizer()
{
    tokenizer_builder builder("HOST");
    builder.add_field("hostname", host_field("hostname", host_platform::host_name));
    builder.add_field("fqdn", host_field("fqdn", host_platform::fqdn));
    builder.add_field("machine_id", host_field("machine_id", host_platform::machine_id));
    builder.add_field("username", host_field("username", host_platform::username));
    return std::move(builder).build();
}

}

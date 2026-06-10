#include "nucleus/completion/completion_generator.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/completion_model.h"

#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/schema/cli_flag.h"

#include "nucleus/keyspace/key_path.h"

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <string_view>

namespace nucleus {

namespace {

// Index a path's declared value set by its canonical string, so a recognized path
// can pick up the allowed_values of the typed element that declares it. surface()
// includes path-tagged registrations that have no typed element; those carry no
// values and simply complete by name.
[[nodiscard]] std::map<std::string, std::vector<std::string>>
value_sets(const schema_registry &schema)
{
    std::map<std::string, std::vector<std::string>> out;
    for(const schema_element &el : schema.elements())
    {
        if(!el.allowed_values.empty())
            out.emplace(el.declared_path().str(), el.allowed_values);
    }
    return out;
}

// Project the schema's recognized surface into the shell-neutral model. surface()
// is deterministically ordered, so the model -- and every generated script -- is
// reproducible. Each path becomes its canonical flag via the same flag_of() the
// argv surface uses, so completion and the real CLI share one mapping.
[[nodiscard]] completion_model project(const schema_registry &schema,
                                       std::string_view prog)
{
    const auto values = value_sets(schema);

    completion_model model;
    model.prog = std::string(prog);
    for(const key_path &path : schema.surface())
    {
        completion_option opt;
        opt.flag = flag_of(path);
        if(auto it = values.find(path.str()); it != values.end())
            opt.values = it->second;
        model.options.push_back(std::move(opt));
    }
    return model;
}

}

std::string generate_completion(shell which, const schema_registry &schema,
                                std::string_view prog)
{
    const completion_model model = project(schema, prog);
    switch(which)
    {
    case shell::zsh:
        return zsh_emitter{}.emit(model);
    case shell::bash:
    default:
        return bash_emitter{}.emit(model);
    }
}

}

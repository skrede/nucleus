#ifndef HPP_GUARD_NUCLEUS_RESOLVE_INSTALLABLE_CHECKS_H
#define HPP_GUARD_NUCLEUS_RESOLVE_INSTALLABLE_CHECKS_H

#include "nucleus/error.h"
#include "nucleus/format.h"
#include "nucleus/expected.h"
#include "nucleus/config_space.h"

#include "nucleus/tokenizer/tokenizer.h"
#include "nucleus/tokenizer/tree_tokenizer.h"

#include "nucleus/utility/escaped_text.h"

#include <any>
#include <string>
#include <optional>
#include <typeindex>
#include <functional>
#include <string_view>

namespace nucleus {

// A host callable that is empty has nothing to call. Consulted at the site that
// would call it, that is a bad_function_call escaping a public entry point; these
// rules refuse it where the host hands it over instead, so the failure names what
// was empty and travels the value channel the seam already returns.

inline registration_result reject_empty_tokenizer_resolver(const tokenizer &tok)
{
    const std::optional<std::string_view> empty = tok.has_empty_resolver();
    if(!empty)
        return registration_ok();
    return unexpected(error{errc::rejected_registration, nucleus::format(
        "tokenizer '{}' carries an empty resolver for '{}'; every field and function must "
        "supply a callable (an empty one has nothing to call when the token is expanded)",
        escaped_text(tok.category()), escaped_text(*empty))});
}

inline registration_result reject_empty_tree_resolver(const tree_tokenizer &tok)
{
    if(!tok.has_empty_resolver())
        return registration_ok();
    return unexpected(error{errc::rejected_registration, nucleus::format(
        "tree tokenizer category '{}' carries an empty resolver; it must supply a callable "
        "(an empty one has nothing to call when the token is evaluated)",
        escaped_text(tok.category()))});
}

inline registration_result reject_empty_converter(
    std::type_index id,
    const std::function<expected<std::any, std::string>(std::string_view)> &conv)
{
    if(conv)
        return registration_ok();
    return unexpected(error{errc::rejected_registration, nucleus::format(
        "converter for type '{}' is empty; a registered converter must supply a callable "
        "(an empty one has nothing to call when a value is coerced)",
        escaped_text(id.name()))});
}

// Naming documents with no factory to open them used to skip the whole inheritance
// expansion and return a configuration the host never asked for, with no signal.
inline registration_result reject_missing_document_factory(const load_options &options)
{
    if(options.document_paths.empty() || options.make_document)
        return registration_ok();
    return unexpected(error{errc::unreadable_source, nucleus::format(
        "document path '{}' cannot be opened: no parser factory was supplied "
        "(make_document is empty, so none of the named documents can be read)",
        escaped_text(options.document_paths.front()))});
}

}

#endif

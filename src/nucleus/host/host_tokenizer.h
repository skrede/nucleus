#ifndef HPP_GUARD_NUCLEUS_HOST_HOST_TOKENIZER_H
#define HPP_GUARD_NUCLEUS_HOST_HOST_TOKENIZER_H

#include "nucleus/tokenizer/tokenizer.h"

namespace nucleus {

// Builds the opt-in HOST tokenizer. This is NOT part of core and is never
// registered automatically: a host that wants machine-identity tokens links
// this module and registers the result explicitly, paying for the platform code
// only when it opts in. Four named fields:
//   ${HOST.hostname}   -> the machine host name
//   ${HOST.fqdn}       -> the fully-qualified domain name
//   ${HOST.machine_id} -> a stable per-installation machine identifier
//   ${HOST.username}   -> the current user's name
// A fact the platform cannot answer surfaces as a resolution error rather than
// an empty expansion, so a missing identity is loud, not silent.
[[nodiscard]] tokenizer make_host_tokenizer();

}

#endif

#ifndef HPP_GUARD_NUCLEUS_HOST_HOST_PLATFORM_H
#define HPP_GUARD_NUCLEUS_HOST_HOST_PLATFORM_H

#include <string>

namespace nucleus::host_platform {

// Platform host facts. These pull OS-specific code (uname / GetComputerName /
// getpwuid / a machine-id file), which is precisely why they live in this
// opt-in module and never in core. Each returns an empty string when the
// platform cannot answer; the tokenizer turns that into a resolution error.

[[nodiscard]] std::string host_name();
[[nodiscard]] std::string fqdn();
[[nodiscard]] std::string machine_id();
[[nodiscard]] std::string username();

}

#endif

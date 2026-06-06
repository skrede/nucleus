#include "nucleus/host/host_platform.h"

#include <array>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#include <lmcons.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <pwd.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#endif

namespace nucleus::host_platform {

std::string host_name()
{
#if defined(_WIN32)
    std::array<char, 256> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if(GetComputerNameExA(ComputerNameDnsHostname, buffer.data(), &size))
        return std::string(buffer.data(), size);
    return {};
#else
    struct utsname info{};
    if(uname(&info) == 0)
        return std::string(info.nodename);
    return {};
#endif
}

std::string fqdn()
{
#if defined(_WIN32)
    std::array<char, 256> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if(GetComputerNameExA(ComputerNameDnsFullyQualified, buffer.data(), &size))
        return std::string(buffer.data(), size);
    return host_name();
#else
    std::array<char, 256> host{};
    if(gethostname(host.data(), host.size()) != 0)
        return host_name();
    host.back() = '\0';

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_CANONNAME;
    addrinfo *info = nullptr;
    if(getaddrinfo(host.data(), nullptr, &hints, &info) != 0 || info == nullptr)
        return std::string(host.data());

    std::string name = (info->ai_canonname != nullptr) ? info->ai_canonname : host.data();
    freeaddrinfo(info);
    return name;
#endif
}

std::string machine_id()
{
#if defined(_WIN32)
    // The cryptographic machine GUID under the registry-backed crypto key. A
    // best-effort read; an empty string when unreadable.
    HKEY key{};
    if(RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography",
                     0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};
    std::array<char, 256> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    DWORD type = 0;
    LONG status = RegQueryValueExA(key, "MachineGuid", nullptr, &type,
                                   reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(key);
    if(status != ERROR_SUCCESS || size == 0)
        return {};
    return std::string(buffer.data());
#else
    // The systemd / dbus machine-id, a stable per-installation identifier.
    for(const char *path : {"/etc/machine-id", "/var/lib/dbus/machine-id"})
    {
        std::ifstream file(path);
        std::string id;
        if(file && std::getline(file, id) && !id.empty())
            return id;
    }
    return {};
#endif
}

std::string username()
{
#if defined(_WIN32)
    std::array<char, UNLEN + 1> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if(GetUserNameA(buffer.data(), &size) && size > 0)
        return std::string(buffer.data(), size - 1);
    return {};
#else
    if(const char *env = std::getenv("USER"))
        return std::string(env);
    if(passwd *pw = getpwuid(geteuid()))
        return std::string(pw->pw_name);
    return {};
#endif
}

}

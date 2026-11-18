#ifndef HPP_GUARD_NUCLEUS_TESTS_COMPLETION_ZSH_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_COMPLETION_ZSH_TEST_SUPPORT_H

#include "completion/bash_test_support.h"
#include "completion/pipe_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <charconv>
#include <iterator>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace nucleus::zsh_test {

// A run names a zsh behind a command prefix through NUCLEUS_ZSH, as a container run does.
inline std::string zsh_command()
{
    const char *named = std::getenv("NUCLEUS_ZSH");
    return named != nullptr && *named != '\0' ? std::string(named) : std::string("zsh");
}

// Windows skips for the reason bash_available gives: a native temp path crosses an unverified translation.
inline bool zsh_available()
{
#ifdef _WIN32
    return false;
#else
    return std::system((zsh_command() + " -fc 'zmodload zsh/zpty' >/dev/null 2>&1").c_str()) == 0;
#endif
}

inline bool below_5_9(const std::string &version)
{
    std::int32_t major      = 0;
    std::int32_t minor      = 0;
    const char  *end        = version.data() + version.size();
    const auto [dot, error] = std::from_chars(version.data(), end, major);
    if(error != std::errc{} || dot == end || *dot != '.' || std::from_chars(dot + 1, end, minor).ec != std::errc{})
        FAIL("cannot read the zsh version '" << version << "'");
    return major < 5 || (major == 5 && minor < 9);
}

enum class zsh_route
{
    fpath,
    source,
    source_bare,
    source_dot,
    source_path,
    eval
};

inline std::string_view route_text(zsh_route route)
{
    switch(route)
    {
        case zsh_route::fpath:       return "fpath";
        case zsh_route::source:      return "source";
        case zsh_route::source_bare: return "source-bare";
        case zsh_route::source_dot:  return "source-dot";
        case zsh_route::source_path: return "source-path";
        case zsh_route::eval:        return "eval";
    }
    return "unknown";
}

struct zsh_script
{
    std::string file;
    std::string text;
};

struct zsh_drive_options
{
    zsh_route                route  = zsh_route::fpath;
    std::string              prog   = "myapp";
    std::string              keys   = "\x18";
    std::string              setup  = "";
    std::vector<std::string> decoys = {};
};

struct zsh_seen
{
    std::string records;
    std::string terminal;
};

struct zsh_session
{
    std::string           version;
    std::string           registered;
    std::string           setup;
    std::string           payload;
    std::vector<zsh_seen> cases;
};

inline void write_file(const std::filesystem::path &file, std::string_view text) { std::ofstream(file, std::ios::binary) << text; }

inline std::string read_file(const std::filesystem::path &file) { return {std::istreambuf_iterator<char>(std::ifstream(file, std::ios::binary).rdbuf()), {}}; }

inline void prepare_drive(const std::filesystem::path &root, const std::vector<zsh_script> &scripts, const std::vector<std::string> &typed, const zsh_drive_options &options)
{
    for(const std::string_view dir : std::array<std::string_view, 4>{"scripts", "in", "work", "out"})
        std::filesystem::create_directory(root / dir);
    for(const zsh_script &script : scripts)
        write_file(root / "scripts" / script.file, script.text);
    for(const std::string &decoy : options.decoys)
        write_file(root / "work" / decoy, "");
    write_file(root / "in" / "route", route_text(options.route));
    write_file(root / "in" / "prog", options.prog);
    write_file(root / "in" / "keys", options.keys);
    write_file(root / "in" / "setup.zsh", options.setup);
    std::string cases;
    for(const std::string &line : typed)
        cases += line + "\n";
    write_file(root / "in" / "cases", cases);
}

inline void run_driver(const std::filesystem::path &root)
{
    bash_test::run_captured(zsh_command() + " -f " + bash_test::shell_quoted(NUCLEUS_ZSH_DRIVER) + " " + bash_test::shell_quoted(root.string()) + " 2>&1");
}

inline zsh_session drive_zsh(const std::vector<zsh_script> &scripts, const std::vector<std::string> &typed, const zsh_drive_options &options = {})
{
    const bash_test::scratch_tree scratch("nucleus_zsh_drive");
    prepare_drive(scratch.path(), scripts, typed, options);
    run_driver(scratch.path());
    const std::filesystem::path out = scratch.path() / "out";
    zsh_session                 session{read_file(out / "version"), read_file(out / "registered"), read_file(out / "setup.tty"), read_file(out / "payload"), {}};
    for(std::size_t k = 1; k <= typed.size(); ++k)
        session.cases.push_back({read_file(out / (std::to_string(k) + ".rec")), read_file(out / (std::to_string(k) + ".tty"))});
    return session;
}

inline std::vector<std::string> records_of(const zsh_seen &seen, std::string_view kind)
{
    const std::string        open = std::string(kind) + "<";
    std::vector<std::string> out;
    std::istringstream       lines(seen.records);
    for(std::string line; std::getline(lines, line);)
        if(line.starts_with(open) && line.ends_with('>'))
            out.push_back(line.substr(open.size(), line.size() - open.size() - 1));
    return out;
}

inline void require_no_noise(const zsh_session &session)
{
    constexpr std::array<std::string_view, 6> noise{"_arguments:", "comparguments", "command not found", "invalid option definition", "invalid argument", "no matches found"};
    std::string                               terminal = session.setup;
    for(const zsh_seen &seen : session.cases)
        terminal += seen.terminal;
    INFO(terminal);
    for(const std::string_view text : noise)
        REQUIRE(terminal.find(text) == std::string::npos);
}

inline void require_quiet(const zsh_session &session)
{
    require_no_noise(session);
    REQUIRE(session.payload.empty());
}
}

#endif

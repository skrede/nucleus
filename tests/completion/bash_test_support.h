#ifndef HPP_GUARD_NUCLEUS_TESTS_COMPLETION_BASH_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_COMPLETION_BASH_TEST_SUPPORT_H

#include <array>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <filesystem>

// MSVC spells the POSIX popen/pclose pair with leading underscores; map them so
// the harness compiles everywhere even though the tests only RUN where the
// bash_available() probe says so.
#ifdef _MSC_VER
    #define popen _popen
    #define pclose _pclose
#endif

namespace nucleus::bash_test {

// Whether a real bash interpreter is on PATH. The smoke tests only run when one
// is; on a box without bash they skip cleanly rather than failing. Windows skips
// deterministically: a Git-Bash may well be installed, but handing it a native
// temp-file path crosses an unverified path-translation boundary -- the bash
// completion contract these smoke tests exercise is a POSIX one.
inline bool bash_available()
{
#ifdef _WIN32
    return false;
#else
    return std::system("bash -c \"exit 0\" >/dev/null 2>&1") == 0;
#endif
}

// Two executables drive bash at once under a parallel CTest, so each scratch name is unique to
// its call and neither can overwrite or remove the other's.
inline std::filesystem::path scratch_path(const std::string &stem)
{
    return std::filesystem::temp_directory_path() / (stem + "_" + std::to_string(std::random_device{}()));
}

// Runs a bash script and returns its stdout. The script is written to a temp file
// and executed; the harness captures its output via popen.
inline std::string run_bash(const std::string &script)
{
    const std::filesystem::path file = scratch_path("nucleus_completion_smoke") += ".sh";
    {
        std::ofstream out(file);
        out << script;
    }
    const std::string cmd = "bash " + file.string() + " 2>/dev/null";
    std::string       captured;
    if(FILE *pipe = popen(cmd.c_str(), "r"))
    {
        std::array<char, 256> buf{};
        while(std::fgets(buf.data(), static_cast<int>(buf.size()), pipe))
            captured += buf.data();
        pclose(pipe);
    }
    std::filesystem::remove(file);
    return captured;
}

// Drives the generated completer with the working directory set to a scratch tree holding
// `decoys`, so a candidate the shell would glob has something in reach to be replaced by.
inline std::string run_seeded(const std::string &completion, const std::string &drive,
                              const std::vector<std::string> &decoys)
{
    const std::filesystem::path dir = scratch_path("nucleus_completion_glob");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for(const std::string &decoy : decoys)
        std::ofstream(dir / decoy);
    const std::string out = run_bash("cd '" + dir.string() + "' || exit 1\n" + completion + drive);
    std::filesystem::remove_all(dir);
    return out;
}

}

#endif

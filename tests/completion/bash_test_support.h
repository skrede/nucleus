#ifndef HPP_GUARD_NUCLEUS_TESTS_COMPLETION_BASH_TEST_SUPPORT_H
#define HPP_GUARD_NUCLEUS_TESTS_COMPLETION_BASH_TEST_SUPPORT_H

#include <array>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <filesystem>

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

// Two executables drive bash at once under a parallel CTest, so each scratch directory is created
// exclusively under a name unique to its call, and neither can overwrite or remove the other's.
inline std::filesystem::path scratch_directory(const std::string &stem)
{
    std::random_device random;
    for(;;)
    {
        std::filesystem::path dir = std::filesystem::temp_directory_path() / (stem + "_" + std::to_string(random()));
        if(std::filesystem::create_directory(dir))
            return dir;
    }
}

inline std::string shell_quoted(const std::string &text)
{
    std::string quoted = "'";
    for(const char c : text)
    {
        if(c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    return quoted + "'";
}

inline void close_pipe(std::FILE *pipe)
{
#ifdef _MSC_VER
    _pclose(pipe);
#else
    pclose(pipe);
#endif
}

using pipe_handle = std::unique_ptr<std::FILE, decltype(&close_pipe)>;

inline pipe_handle opened_pipe(const std::string &command)
{
#ifdef _MSC_VER
    return pipe_handle(_popen(command.c_str(), "r"), &close_pipe);
#else
    return pipe_handle(popen(command.c_str(), "r"), &close_pipe);
#endif
}

inline std::string run_bash(const std::string &script)
{
    const std::filesystem::path scratch = scratch_directory("nucleus_completion_smoke");
    const std::filesystem::path file    = scratch / "script.sh";
    std::ofstream(file) << script;
    std::string captured;
    if(const pipe_handle pipe = opened_pipe("bash " + shell_quoted(file.string()) + " 2>/dev/null"))
    {
        std::array<char, 256> buf{};
        while(std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()))
            captured += buf.data();
    }
    std::filesystem::remove_all(scratch);
    return captured;
}

// Pressing Enter parses the line readline built. readline inserts each reply verbatim after the
// typed word's opening quote, if any, and closes that quote unless the reply already ends with it.
inline std::string insert_and_parse(const std::vector<std::string> &words, const std::string &prefix)
{
    const char        opening = words.empty() || words.back().empty() ? '\0' : words.back().front();
    const std::string quote   = opening == '\'' || opening == '"' ? std::string(1, opening) : std::string();
    std::string       out     = "\nmyapp() { for a in \"$@\"; do printf '<%s>' \"$a\"; done; echo; }\nCOMP_WORDS=(myapp";
    for(const std::string &word : words)
        out += " " + shell_quoted(word);
    out += ")\nCOMP_CWORD=$((${#COMP_WORDS[@]} - 1))\n_myapp_complete\n";
    out += "p=" + shell_quoted(prefix) + "; q=" + shell_quoted(quote) + "\n";
    out += "for c in \"${COMPREPLY[@]}\"; do case \"$c\" in *\"$q\") l=\"$c\" ;; *) l=\"$c$q\" ;; esac; eval \"myapp $p$l\"; done\n";
    return out + "for f in PWN_*; do [ -e \"$f\" ] && echo \"PAYLOAD $f\"; done\n";
}

// Drives the generated completer with the working directory set to a scratch tree holding
// `decoys`, so a candidate the shell would glob has something in reach to be replaced by.
inline std::string run_seeded(const std::string &completion, const std::string &drive,
                              const std::vector<std::string> &decoys)
{
    const std::filesystem::path dir = scratch_directory("nucleus_completion_glob");
    for(const std::string &decoy : decoys)
        std::ofstream(dir / decoy);
    const std::string out = run_bash("cd " + shell_quoted(dir.string()) + " || exit 1\n" + completion + drive);
    std::filesystem::remove_all(dir);
    return out;
}

}

#endif

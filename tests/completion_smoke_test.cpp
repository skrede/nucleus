#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <fstream>
#include <filesystem>

using nucleus::shell;
using nucleus::anchor;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::generate_completion;

// MSVC spells the POSIX popen/pclose pair with leading underscores; map them so
// the harness compiles everywhere even though the test only RUNS where the
// bash_available() probe below says so.
#ifdef _MSC_VER
    #define popen _popen
    #define pclose _pclose
#endif

namespace {

key_path path_of(const char *text) { return key_path::parse(text).value(); }

schema_registry fixture()
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("logging", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("level", anchor::keyspace(path_of("logging")),
                                     {"debug", "info", "warn", "error"})));
    return reg;
}

// Whether a real bash interpreter is on PATH. The smoke test only runs when one
// is; on a box without bash it skips cleanly rather than failing. Windows skips
// deterministically: a Git-Bash may well be installed, but handing it a native
// temp-file path crosses an unverified path-translation boundary -- the bash
// completion contract this smoke test exercises is a POSIX one.
bool bash_available()
{
#ifdef _WIN32
    return false;
#else
    return std::system("bash -c \"exit 0\" >/dev/null 2>&1") == 0;
#endif
}

// Runs a bash script and returns its stdout. The script is written to a temp file
// and executed; the harness captures the single line of output via popen.
std::string run_bash(const std::string &script)
{
    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / "nucleus_completion_smoke.sh";
    {
        std::ofstream out(file);
        out << script;
    }
    const std::string cmd = "bash " + file.string() + " 2>/dev/null";
    std::string captured;
    if(FILE *pipe = popen(cmd.c_str(), "r"))
    {
        std::array<char, 256> buf{};
        while(std::fgets(buf.data(), static_cast<int>(buf.size()), pipe))
            captured += buf.data();
        pclose(pipe);
    }
    fs::remove(file);
    return captured;
}

}

TEST_CASE("the generated bash script actually completes under real bash",
          "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- the real-bash smoke test needs it");

    const std::string completion = generate_completion(shell::bash, fixture(), "myapp");

    // Drive the sourced function the way bash itself would: set COMP_WORDS /
    // COMP_CWORD and invoke the completer, then print COMPREPLY.

    // Case 1: flag-name completion for a "--log" partial.
    {
        const std::string script = completion +
            "\nCOMP_WORDS=(myapp --log)\nCOMP_CWORD=1\n_myapp_complete\n"
            "echo \"${COMPREPLY[@]}\"\n";
        const std::string out = run_bash(script);
        REQUIRE(out.find("--logging") != std::string::npos);
        REQUIRE(out.find("--logging-level") != std::string::npos);
    }

    // Case 2: the COMP_WORDBREAKS value case. bash splits "--logging-level=" into
    // the words [--logging-level][=][""], so the function must reconstruct the
    // flag and complete the value tail.
    {
        const std::string script = completion +
            "\nCOMP_WORDS=(myapp --logging-level = \"\")\nCOMP_CWORD=3\n"
            "_myapp_complete\necho \"${COMPREPLY[@]}\"\n";
        const std::string out = run_bash(script);
        REQUIRE(out.find("debug") != std::string::npos);
        REQUIRE(out.find("info") != std::string::npos);
        REQUIRE(out.find("warn") != std::string::npos);
        REQUIRE(out.find("error") != std::string::npos);
    }

    // Case 3: a partial value filters the candidate set.
    {
        const std::string script = completion +
            "\nCOMP_WORDS=(myapp --logging-level = w)\nCOMP_CWORD=3\n"
            "_myapp_complete\necho \"${COMPREPLY[@]}\"\n";
        const std::string out = run_bash(script);
        REQUIRE(out.find("warn") != std::string::npos);
        REQUIRE(out.find("debug") == std::string::npos);
    }
}

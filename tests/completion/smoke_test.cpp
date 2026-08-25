#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>
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

    const std::string completion = generate_completion(shell::bash, fixture(), "myapp").value();

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

TEST_CASE("an allowed value containing a space completes as one candidate",
          "[completion][smoke]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("region", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("zone", anchor::keyspace(path_of("region")),
                                     {"us east", "other"})));
    const std::string completion = generate_completion(shell::bash, reg, "myapp").value();

    // The separator inside the value is escaped; the one between the two values is not.
    REQUIRE(completion.find("'us\\ east other'") != std::string::npos);

    if(!bash_available())
        return;

    const std::string script = completion +
        "\nCOMP_WORDS=(myapp --region-zone = us)\nCOMP_CWORD=3\n"
        "_myapp_complete\nprintf '%s\\n' \"${#COMPREPLY[@]}\" \"${COMPREPLY[0]}\"\n";
    const std::string out = run_bash(script);
    REQUIRE(out == "1\nus east\n");
}

TEST_CASE("a space name places the ordinal wildcard on the segment the path reaches",
          "[completion][smoke]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("node")))));

    const std::string two = generate_completion(shell::bash, reg, "myapp", {}, {}, "a/b").value();
    REQUIRE(two.find("--a-b-node-*-port") != std::string::npos);
    REQUIRE(two.find("--a-b-*-node-port") == std::string::npos);

    // The one- and zero-segment shapes are what the replaced ternary already got right; they
    // are asserted here so widening the count cannot move them.
    const std::string one = generate_completion(shell::bash, reg, "myapp", {}, {}, "a").value();
    REQUIRE(one.find("--a-node-*-port") != std::string::npos);
    const std::string none = generate_completion(shell::bash, reg, "myapp").value();
    REQUIRE(none.find("--node-*-port") != std::string::npos);
}

// `compgen -W` EXPANDS every word it produces, so a word list is a code channel. The payloads are
// chosen from what a shell does to a word rather than from what the emitter escapes -- a set drawn
// from the escaper's own character list agrees with it by construction and can falsify nothing.
// The three that can run a command each write the sentinel; the fourth gathers what only rewrites a
// word -- tilde, arithmetic and brace expansion, redirection, control operators, a bidi override.
TEST_CASE("no shell construct in a word list fires or corrupts the candidate",
          "[completion][smoke]")
{
    namespace fs = std::filesystem;
    const fs::path sentinel = fs::temp_directory_path() / "nucleus_completion_sentinel";
    const std::string s = sentinel.string();
    const std::vector<std::string> payloads{
        "a<(:>" + s + ")b",     // process substitution
        "a$(touch " + s + ")b", // command substitution
        "a`touch " + s + "`b",  // its backquoted spelling
        "~root/z$((6*7)){x,y}>q&r;t|u\xe2\x80\xae" "b"};
    nucleus::completion_model model{"myapp", {{"--v", "d", payloads}}};
    std::string expected = "--v\n";
    for(const std::string &payload : payloads)
    {
        model.options.push_back({"--" + payload, "d", {}});
        expected += "--" + payload + "\n";
    }
    // No zsh interpreter is assumed to exist here, so the zsh half is pinned by text alone.
    REQUIRE(nucleus::zsh_emitter{}.emit(model).find("a<(") == std::string::npos);
    if(!bash_available())
        return;

    fs::remove(sentinel);
    const std::string script = nucleus::bash_emitter{}.emit(model)
        + "\nCOMP_WORDS=(myapp -)\nCOMP_CWORD=1\n_myapp_complete\n"
          "printf '%s\\n' \"${COMPREPLY[@]}\"\n"
          "COMP_WORDS=(myapp --v = \"\")\nCOMP_CWORD=3\n_myapp_complete\n"
          "printf '%s\\n' \"${COMPREPLY[@]}\"\n";
    const std::string out = run_bash(script);
    REQUIRE_FALSE(fs::exists(sentinel));
    for(const std::string &payload : payloads)
        expected += payload + "\n";
    REQUIRE(out == expected);
}

namespace {

// Drives the generated completer with the working directory set to a scratch tree holding
// `decoys`, so a candidate the shell would glob has something in reach to be replaced by.
std::string run_seeded(const std::string &completion, const std::string &drive,
                       const std::vector<std::string> &decoys)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "nucleus_completion_glob";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for(const std::string &decoy : decoys)
        std::ofstream(dir / decoy);
    const std::string out = run_bash("cd '" + dir.string() + "' || exit 1\n" + completion + drive);
    fs::remove_all(dir);
    return out;
}

}

// The ordinal wildcard needs no host cooperation to be glob-expanded: it is the emitter's own
// text, and every repeated-container schema emits one.
TEST_CASE("a directory holding the ordinal wildcard's matches cannot replace it",
          "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    schema_registry reg;
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("node")))));

    const std::string out = run_seeded(
        generate_completion(shell::bash, reg, "myapp").value(),
        "\nCOMP_WORDS=(myapp --node)\nCOMP_CWORD=1\n_myapp_complete\n"
        "printf '%s\\n' \"${COMPREPLY[@]}\"\n",
        {"--node-7-port", "--node-9-port"});
    REQUIRE(out == "--node\n--node-port\n--node-*-port\n");
}

TEST_CASE("an allowed value carrying a glob is offered literally", "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    schema_registry reg;
    REQUIRE(reg.attach(nucleus::element("region", anchor::root())));
    REQUIRE(reg.attach(nucleus::enum_element("zone", anchor::keyspace(path_of("region")),
                                     {"*", "us-east"})));

    const std::string out = run_seeded(
        generate_completion(shell::bash, reg, "myapp").value(),
        "\nCOMP_WORDS=(myapp --region-zone = \"\")\nCOMP_CWORD=3\n_myapp_complete\n"
        "printf '%s\\n' \"${COMPREPLY[@]}\"\n",
        {"all", "any"});
    REQUIRE(out == "*\nus-east\n");
}

// Suppressing the glob is a change to the shell the completer is invoked from, so it has to be
// undone -- and only when this function was what made it.
TEST_CASE("driving the completion leaves the caller's glob setting as it found it",
          "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- driving the generated script needs it");

    const std::string completion = generate_completion(shell::bash, fixture(), "myapp").value();
    const std::string drive =
        "\nbefore=$-\nCOMP_WORDS=(myapp --log)\nCOMP_CWORD=1\n_myapp_complete\n"
        "COMP_WORDS=(myapp --logging-level = \"\")\nCOMP_CWORD=3\n_myapp_complete\n"
        "if [ \"$-\" = \"$before\" ]; then echo kept; else echo \"$before -> $-\"; fi\n";
    REQUIRE(run_bash("set +f\n" + completion + drive) == "kept\n");
    REQUIRE(run_bash("set -f\n" + completion + drive) == "kept\n");
}

// The description is the one host-supplied field that is prose, so it cannot take the word
// list's byte allowlist -- that would backslash every space the tool emits. What can leave a
// `[...]` description is enumerated instead: `]` ends it and puts what follows into the spec's
// action, the one field _arguments evaluates; `[` moves where zsh reads the end; a trailing `\`
// swallows the terminator; `:` separates the spec's fields. Nothing else there is structure, and
// the single quote wrapping each spec is what holds the rest inert. No zsh exists on this
// machine, so these pin the emitted text and the behavior they imply is unverified.
TEST_CASE("a hostile option description cannot leave the zsh spec entry", "[completion]")
{
    const auto emitted = [](const std::string &description) {
        return nucleus::zsh_emitter{}.emit({"myapp", {{"--f", description, {"v1"}}}});
    };

    // The breakout: an unescaped `]` would hand `:*:->state` to the action field.
    REQUIRE(emitted("d]:*:->state").find("'--f=[d\\]\\:*\\:->state]:value:(v1)'")
            != std::string::npos);
    // A trailing backslash reaches the same field by swallowing the terminator.
    REQUIRE(emitted("d\\").find("'--f=[d\\\\]:value:(v1)'") != std::string::npos);
    REQUIRE(emitted("a[b").find("'--f=[a\\[b]:value:(v1)'") != std::string::npos);

    // Inert unescaped, because the spec is one single-quoted word; a backslash on any of these
    // would reach the menu rather than zsh, which strips one only where it documents doing so.
    REQUIRE(emitted("d$(id)`id`;id|id&").find("'--f=[d$(id)`id`;id|id&]:value:(v1)'")
            != std::string::npos);
    // The one byte that wrapper does treat as special closes and reopens it instead of ending it.
    REQUIRE(emitted("d'x").find("'--f=[d'\\''x]:value:(v1)'") != std::string::npos);

    // No non-printable byte reaches the spec: a newline cannot break it across the script's
    // lines, and an escape sequence cannot reach the terminal that draws the menu.
    REQUIRE(emitted("a\nb\x1b[2Jc\x7f\x07").find("'--f=[a^Jb^\\[\\[2Jc^?^G]:value:(v1)'")
            != std::string::npos);

    // Prose keeps its spaces and its ordinary punctuation; only the colon carries a mark, and
    // zsh strips that one before display.
    REQUIRE(emitted("set the level (default: info), e.g. \"warn\" -- 100% sure")
                .find("[set the level (default\\: info), e.g. \"warn\" -- 100% sure]")
            != std::string::npos);
}

#include "completion/bash_test_support.h"

#include "nucleus/completion/zsh_emitter.h"
#include "nucleus/completion/bash_emitter.h"
#include "nucleus/completion/completion_generator.h"

#include "nucleus/schema/anchor.h"
#include "nucleus/schema/schema.h"
#include "nucleus/schema/schema_registry.h"

#include "nucleus/keyspace/key_path.h"

#include "support/completion_result_test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <filesystem>

using nucleus::anchor;
using nucleus::generate_completion;
using nucleus::key_path;
using nucleus::schema_registry;
using nucleus::shell;
using nucleus::bash_test::bash_available;
using nucleus::bash_test::run_bash;
using nucleus::bash_test::run_seeded;
using nucleus::bash_test::scratch_directory;
using nucleus::completion_result_test::script_of;

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
}

TEST_CASE("the generated bash script actually completes under real bash",
          "[completion][smoke]")
{
    if(!bash_available())
        SKIP("bash not found on PATH -- the real-bash smoke test needs it");

    const std::string completion = script_of(generate_completion(shell::bash, fixture(), "myapp"));

    {
        const std::string script = completion +
            "\nCOMP_WORDS=(myapp --log)\nCOMP_CWORD=1\n_myapp_complete\n"
            "echo \"${COMPREPLY[@]}\"\n";
        const std::string out = run_bash(script);
        REQUIRE(out.find("--logging") != std::string::npos);
        REQUIRE(out.find("--logging-level") != std::string::npos);
    }

    // bash 4 and later split "--logging-level=" into the words [--logging-level][=], so `=` is
    // the current word and the function must rebuild the flag from the word before it.
    {
        const std::string script = completion +
                "\nCOMP_WORDS=(myapp --logging-level =)\nCOMP_CWORD=2\n"
                "_myapp_complete\necho \"${COMPREPLY[@]}\"\n";
        const std::string out = run_bash(script);
        REQUIRE(out.find("debug") != std::string::npos);
        REQUIRE(out.find("info") != std::string::npos);
        REQUIRE(out.find("warn") != std::string::npos);
        REQUIRE(out.find("error") != std::string::npos);
    }

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
    const std::string completion = script_of(generate_completion(shell::bash, reg, "myapp"));

    // The separator inside the value is escaped; the one between the two values is not.
    REQUIRE(completion.find("'us\\ east other'") != std::string::npos);

    if(!bash_available())
        return;

    const std::string script = completion +
            "\nCOMP_WORDS=(myapp --region-zone = us)\nCOMP_CWORD=3\n"
            "_myapp_complete\nprintf '%s\\n' \"${#COMPREPLY[@]}\"; eval \"printf '%s\\n' ${COMPREPLY[0]}\"\n";
    const std::string out = run_bash(script);
    REQUIRE(out == "1\nus east\n");
}

TEST_CASE("a space name places the ordinal wildcard on the segment the path reaches",
          "[completion][smoke]")
{
    schema_registry reg;
    REQUIRE(reg.attach(nucleus::repeated_element("node", anchor::root())));
    REQUIRE(reg.attach(nucleus::element("port", anchor::keyspace(path_of("node")))));

    const std::string two = script_of(generate_completion(shell::bash, reg, "myapp", {}, {}, "a/b"));
    REQUIRE(two.find("--a-b-node-*-port") != std::string::npos);
    REQUIRE(two.find("--a-b-*-node-port") == std::string::npos);

    // The one- and zero-segment shapes are what the replaced ternary already got right; they
    // are asserted here so widening the count cannot move them.
    const std::string one = script_of(generate_completion(shell::bash, reg, "myapp", {}, {}, "a"));
    REQUIRE(one.find("--a-node-*-port") != std::string::npos);
    const std::string none = script_of(generate_completion(shell::bash, reg, "myapp"));
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
    const fs::path                 dir = scratch_directory("nucleus_completion_word_list");
    const std::string              s   = (dir / "sentinel").string();
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
    {
        fs::remove_all(dir);
        return;
    }
    const std::string script = nucleus::bash_emitter{}.emit(model);
    const std::string drive  = "\nCOMP_WORDS=(myapp -)\nCOMP_CWORD=1\n_myapp_complete\n"
                               "for c in \"${COMPREPLY[@]}\"; do eval \"printf '%s\\n' $c\"; done\n"
                               "COMP_WORDS=(myapp --v =)\nCOMP_CWORD=2\n_myapp_complete\n"
                               "for c in \"${COMPREPLY[@]}\"; do eval \"printf '%s\\n' $c\"; done\n";
    const std::string out    = run_bash(script + drive);
    REQUIRE(fs::is_empty(dir));
    fs::remove_all(dir);
    for(const std::string &payload : payloads)
        expected += payload + "\n";
    REQUIRE(out == expected);
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
            script_of(generate_completion(shell::bash, reg, "myapp")),
            "\nCOMP_WORDS=(myapp --node)\nCOMP_CWORD=1\n_myapp_complete\n"
            "for c in \"${COMPREPLY[@]}\"; do eval \"printf '%s\\n' $c\"; done\n",
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
            script_of(generate_completion(shell::bash, reg, "myapp")),
            "\nCOMP_WORDS=(myapp --region-zone =)\nCOMP_CWORD=2\n_myapp_complete\n"
            "for c in \"${COMPREPLY[@]}\"; do eval \"printf '%s\\n' $c\"; done\n",
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

    const std::string completion = script_of(generate_completion(shell::bash, fixture(), "myapp"));
    const std::string drive =
            "\nbefore=$-\nCOMP_WORDS=(myapp --log)\nCOMP_CWORD=1\n_myapp_complete\n"
            "COMP_WORDS=(myapp --logging-level =)\nCOMP_CWORD=2\n_myapp_complete\n"
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

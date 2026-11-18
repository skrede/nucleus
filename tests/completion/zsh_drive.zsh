# Usage: zsh -f zsh_drive.zsh S. Drives an interactive zsh in a pty through the cases in S/in, installing
# the scripts in S/scripts the way a user's .zshrc does, and writes what compadd was handed and every byte
# the terminal carried to S/out. Exits 0 when every case ran, non-zero naming what failed otherwise.

typeset -F SECONDS
nucleus_scratch=$1
nucleus_child=${0:a:h}/zsh_drive_child.zsh
nucleus_got=

nucleus_fail()
{
    print -r -- "$1"
    zpty -d nucleus_z 2>/dev/null
    exit 1
}

nucleus_locale()
{
    local l
    zmodload zsh/langinfo || nucleus_fail "cannot load zsh/langinfo"
    unset LC_ALL LC_CTYPE
    for l in C.UTF-8 en_US.UTF-8; do
        LANG=$l
        if [[ $langinfo[CODESET] == UTF-8 ]]; then
            export LANG
            return 0
        fi
    done
    nucleus_fail "no UTF-8 locale among C.UTF-8 and en_US.UTF-8"
}

# zpty -r -t never blocks, so every wait is bounded by the deadline below and nothing waits a fixed time.
nucleus_await()
{
    local chunk
    local -F deadline=$(( SECONDS + 30 ))
    while [[ $nucleus_got != *$1* ]]; do
        if zpty -r -t nucleus_z chunk; then
            nucleus_got+=$chunk
        elif (( SECONDS > deadline )); then
            nucleus_fail "timeout waiting for $1"
        else
            zselect -t 1
        fi
    done
}

nucleus_take()
{
    local head=${nucleus_got%%$1*}
    print -rn -- "$head$1" > $2
    nucleus_got=${nucleus_got#*$1}
}

nucleus_start()
{
    zpty nucleus_z "cd ${(q)nucleus_scratch}/work && zsh -f -i"
    zpty -w nucleus_z "source ${(q)nucleus_child} ${(q)nucleus_scratch}"
    # The quotes split the sentinel in the echoed command line, so only the command's output matches.
    zpty -w nucleus_z "print -r -- NUCLEUS''_READY"
    nucleus_await NUCLEUS_READY
    nucleus_take NUCLEUS_READY $nucleus_scratch/out/setup.tty
    [[ $(<$nucleus_scratch/out/version) == $ZSH_VERSION ]] ||
        nucleus_fail "child zsh $(<$nucleus_scratch/out/version) is not zsh $ZSH_VERSION"
}

nucleus_case()
{
    local sentinel=NUCLEUS_DONE_$2_
    nucleus_await NUCLEUS_ZLE
    zpty -w -n nucleus_z "$1$nucleus_keys"
    zpty -w nucleus_z $'\x15'"print -r -- NUCLEUS''_DONE_$2_; (( ++nucleus_case ))"
    nucleus_await $sentinel
    nucleus_take $sentinel $nucleus_scratch/out/$2.tty
}

nucleus_payload()
{
    local -a found
    found=( $nucleus_scratch/work/PWN_*(N:t) )
    : > $nucleus_scratch/out/payload
    (( $#found )) && print -rl -- $found > $nucleus_scratch/out/payload
    return 0
}

nucleus_main()
{
    local -a cases
    local k
    print -r -- "zsh $ZSH_VERSION"
    zmodload zsh/zpty || nucleus_fail "cannot load zsh/zpty"
    zmodload zsh/zselect || nucleus_fail "cannot load zsh/zselect"
    nucleus_locale
    unset TERM
    IFS= read -r -d '' nucleus_keys < $nucleus_scratch/in/keys
    cases=( ${(f)"$(<$nucleus_scratch/in/cases)"} )
    (( $#cases )) || nucleus_fail "no case in $nucleus_scratch/in/cases"
    nucleus_start
    for k in {1..$#cases}; do
        nucleus_case $cases[k] $k
    done
    nucleus_payload
    zpty -d nucleus_z
    exit 0
}

nucleus_main

nucleus_scratch=$1
nucleus_case=1

nucleus_record()
{
    print -r -- "$1" >> $nucleus_scratch/out/$nucleus_case.rec
}

compadd()
{
    local -a hits shown
    local i name
    if [[ " $* " == *" -"[OAD]" "* ]]; then
        builtin compadd "$@"
        return
    fi
    builtin compadd -A hits "$@"
    for (( i = 1; i < $#; i++ )); do
        [[ ${@[i]} == -d ]] && name=${@[i+1]}
    done
    [[ -n $name ]] && shown=( ${(P)name} )
    for i in $hits; do
        nucleus_record "HIT<$i>"
        nucleus_record "VALUE<${(Q)i}>"
    done
    for i in $shown; do
        nucleus_record "DISP<$i>"
    done
    builtin compadd "$@"
}

nucleus_probe()
{
    zle complete-word
    nucleus_record "BUF<$BUFFER>"
    BUFFER=
    zle -R
}

nucleus_source_scripts()
{
    local f
    local -a kept
    # (N): an empty directory must not abort the loop under NOMATCH.
    for f in $nucleus_scratch/scripts/*(N); do
        case $1 in
            (source) source $f ;;
            (source-bare) cd $f:h && source $f:t ;;
            (source-dot) cd $f:h && source ./$f:t ;;
            (source-path) kept=( $path ); path=( $f:h $path ); source $f:t; path=( $kept ) ;;
            (eval) eval "$(<$f)" ;;
        esac
    done
    cd $nucleus_scratch/work
}

nucleus_install()
{
    local route="$(<$nucleus_scratch/in/route)"
    if [[ $route == fpath ]]; then
        fpath=( $nucleus_scratch/scripts $fpath )
    fi
    autoload -U compinit && compinit -u -d $nucleus_scratch/out/zcompdump
    if [[ $route == (source*|eval) ]]; then
        nucleus_source_scripts $route
    fi
    source $nucleus_scratch/in/setup.zsh
}

# zle sets the terminal raw before it draws the prompt, so the driver writes a case only once it sees this marker.
PS1='NUCLEUS_ZLE%# '
RPS1=
setopt nobeep
bindkey -e
# ^X opens many emacs bindings; with none left under it, ^X followed by ^U is two keys, not undo.
bindkey -rp '^X'
zle -N nucleus_probe
bindkey '^X' nucleus_probe
# KEYTIMEOUT is in hundredths of a second; any key still opening a sequence waits no longer than that.
KEYTIMEOUT=1
COLUMNS=400
LINES=1000
LISTMAX=100000
zstyle ':completion:*' list-prompt ''
zstyle ':completion:*' select-prompt ''
nucleus_install
print -rn -- $ZSH_VERSION > $nucleus_scratch/out/version
print -rn -- ${_comps[$(<$nucleus_scratch/in/prog)]-unset} > $nucleus_scratch/out/registered

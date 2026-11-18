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

nucleus_install()
{
    local f
    if [[ $(<$nucleus_scratch/in/route) == fpath ]]; then
        fpath=( $nucleus_scratch/scripts $fpath )
    fi
    autoload -U compinit && compinit -u -d $nucleus_scratch/out/zcompdump
    if [[ $(<$nucleus_scratch/in/route) == source ]]; then
        # (N): an empty directory must not abort the loop under NOMATCH.
        for f in $nucleus_scratch/scripts/*(N); do
            source $f
        done
    fi
    source $nucleus_scratch/in/setup.zsh
}

PS1='%# '
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

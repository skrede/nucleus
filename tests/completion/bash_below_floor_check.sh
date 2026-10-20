#!/bin/sh
# Sources every *.bash in the working directory under the bash running this check. Each must print nothing on
# stderr, return status 0 and leave no completion registered.
echo "bash $BASH_VERSION"
err=$(mktemp) || exit 1
trap 'rm -f "$err"' EXIT
fail=0
n=0
for f in *.bash; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    out=$(bash -c '. "./$1"; s=$?; complete -p; exit $s' _ "$f" 2>"$err")
    s=$?
    if [ "$s" -ne 0 ] || [ -s "$err" ] || [ -n "$out" ]; then
        echo "FAIL $f: status $s, stderr [$(cat "$err")], complete -p [$out]"
        fail=1
    else
        echo "ok $f"
    fi
done
if [ "$n" -eq 0 ]; then
    echo "no fixture scripts"
    exit 1
fi
exit "$fail"

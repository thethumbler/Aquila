#!/bin/bash
# The bash test's script (tests/run.sh): bash's own features on AquilaOS,
# each printing a line the test looks for

# arrays, associative arrays, arithmetic, [[ ]]
a=(one two three); a+=(four)
declare -A m=([x]=1 [y]=2)
(( n = ${#a[@]} * m[y] ))
[[ ${a[2]} == t*e && $n -eq 8 ]] && echo "arrays: ${a[*]} n=$n"

# string operations, here-string, mapfile, read
s="Hello, AquilaOS"
echo "strings: ${s,,} ${s#*, } ${s/Aquila/bash} ${#s}"
mapfile -t lines <<< $'a\nb\nc'
read -r first rest <<< "x y z"
echo "mapfile: ${#lines[@]} ${lines[1]} read: $first|$rest"

# functions, locals, recursion
fact() { local k=$1; (( k <= 1 )) && echo 1 || echo $(( k * $(fact $((k - 1))) )); }
echo "functions: $(fact 10)"

# process substitution (named pipes), pipefail, pipelines
echo "procsub: $(cat <(echo left) <(echo right) | tr '\n' ' ')"
set -o pipefail
false | true; echo "pipefail: $?"
set +o pipefail

# background jobs, wait, trap
sleep 1 & p1=$!
( exit 7 ) & p2=$!
wait $p1; r1=$?; wait $p2; r2=$?
echo "jobs: $r1 $r2"
trap 'echo "trap: USR1"' USR1
kill -USR1 $$

# extended globs, brace expansion, printf
shopt -s extglob
cd /tmp && mkdir -p g && cd g && touch a.c b.h c.o
echo "globs: $(echo !(*.o))" {1..3} "$(printf '%03d' 7)"

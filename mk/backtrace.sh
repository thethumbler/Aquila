#!/bin/sh
#
# A panic's stack trace, its addresses named: the function, the file and
# the line of each, from the kernel's ELF file (its symbols; the lines
# need a DEBUG=1 build, make debug's).
#
#   mk/backtrace.sh <kernel> [<log>]      (the serial log, or stdin)
#
# e.g. mk/backtrace.sh build/min-x86_64/kernel/kernel-0.0.1.x86_64 < serial.log
#

if [ $# -lt 1 ] || [ ! -f "$1" ]; then
    echo "usage: $0 <kernel ELF file> [<log>]" >&2
    exit 2
fi

kernel=$1
shift

# LLVM's reads both compilers' line tables (aarch64's kernel is clang's),
# binutils' only gcc's
ADDR2LINE=${ADDR2LINE:-$(command -v llvm-addr2line || echo addr2line)}

# the lines that are an address in brackets: "  [0xFFFFFFFF80101CC7]"
sed -n 's/^.*\[\(0x[0-9A-Fa-f]*\)\].*$/\1/p' "$@" |
while read -r addr; do
    printf '%s  ' "$addr"
    # a return address is just past the call: the line is the call's
    $ADDR2LINE -f -i -p -C -e "$kernel" "$(printf '0x%x' $((addr - 1)))"
done

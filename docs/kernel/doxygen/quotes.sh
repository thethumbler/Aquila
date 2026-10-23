#!/bin/sh
# Doxygen's input filter: the kernel's comments quote names `like this',
# which Markdown would take for an unclosed code span; `like this` instead
exec sed "s/\`\([^\`']*\)'/\`\1\`/g" "$1"

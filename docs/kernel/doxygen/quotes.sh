#!/bin/sh
# Doxygen's input filter: older comments quote names `like_this', which
# Markdown would take for an unclosed code span; `like_this` instead. Only
# a quoted word, with no spaces, is changed: a sentence with a code span
# and an apostrophe after it (`foo()` doesn't) is left alone.
exec sed "s/\`\([^\` ']\{1,\}\)'/\`\1\`/g" "$1"

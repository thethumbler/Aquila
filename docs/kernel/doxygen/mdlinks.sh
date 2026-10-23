#!/bin/sh
# Doxygen's input filter for the documents: their links to the sources
# (../../kernel/..., right on GitHub) are just the code's name here
exec sed 's/\[\(`[^]]*`\)\](\(\.\.\/\)*kernel\/[^)]*)/\1/g' "$1"

#!/bin/sh
# Doxygen's input filter for the documents: their links into the
# repository (../../kernel/..., ../../Makefile, right on GitHub) are just
# their text here
exec sed 's/\[\([^]]*\)\](\(\.\.\/\)\{2,\}[^)]*)/\1/g' "$1"

#!/usr/bin/env bash
# Source gate for FR-005 "no heap allocation" (spec 004-driver): fails when a
# checked C source calls `malloc`, `calloc`, `realloc`, `free` or `strdup`.
#
# Usage: check-no-heap.sh [file ...]
#   file  C sources to scan (default: src/hamiltonian/driver.c src/io/config.c)
# Exit: 0 = no heap call found, 1 = a heap call found, 2 = usage or missing file.
# Owner: caller; read-only, writes nothing. Comments are not stripped: a
# source that must mention these names in prose has to avoid the call form
# `name(`; a hyphenated word such as `matrix-free (` is not a call.
set -u

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || exit 2
cd "$root" || exit 2

if [ "$#" -eq 0 ]; then
  set -- src/hamiltonian/driver.c src/io/config.c
fi

bad=0
for f in "$@"; do
  [ -f "$f" ] || { printf 'check-no-heap: %s not found\n' "$f" >&2; exit 2; }
  if grep -n -E '(^|[^-A-Za-z0-9_])(malloc|calloc|realloc|free|strdup)[[:space:]]*\(' "$f"; then
    printf 'check-no-heap: heap call in %s\n' "$f" >&2
    bad=1
  fi
done
exit "$bad"

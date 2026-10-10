#!/bin/sh
# Source gates of the evolution library (spec 005-evolution, T-016; FR-001,
# FR-021, FR-028; constitution ARC-1, MEM-1). Scans every `.c` and `.h` file
# below a directory, with comments removed so that documentation may name the
# forbidden things, and fails on:
#   io     an `#include` of a header under an `io/` directory (FR-028:
#          `evolution` does not depend on `io`).
#   dense  a `dim * dim` product or a `[dim][dim]` array, the shapes of a dense
#          Hamiltonian matrix (FR-001, ARC-1).
#   alloc  a call of `malloc`, `calloc`, `realloc`, `aligned_alloc`, `strdup` or
#          `free` outside the documented owner (FR-021). The owner is the file
#          `evolve.c`, whose only allowed lines are the two default hooks
#          `return calloc(count, size);` and `free(ptr);`.
#
# Usage: check-005-evolution-gate.sh <directory>
# Exit: 0 = clean; 1 = io include; 2 = dense matrix shape; 3 = allocation
#       outside the owner (the first failing class in that order decides the
#       code, every class is reported); 64 = usage error.
# Owner: caller; reads files, writes only to stdout/stderr and one temporary
#       file removed on every path. Requires `perl` for comment removal.

set -eu

if [ "$#" -ne 1 ] || [ ! -d "$1" ]; then
    printf 'usage: %s <directory>\n' "$0" >&2
    exit 64
fi

root=$1
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT HUP INT TERM
code=0
iocode=0
densecode=0
alloccode=0

# Allocation words, and the owner file name (documented in evolve.c, FR-021).
alloc_re='\b(malloc|calloc|realloc|aligned_alloc|strdup|free)\b'

files=$(find "$root" -type f \( -name '*.c' -o -name '*.h' \) | LC_ALL=C sort)

for file in $files; do
    # Remove block and line comments; keep the line count so numbers match.
    perl -0pe 's{/\*.*?\*/}{ my $c = $&; $c =~ s/[^\n]//g; $c }gse; s{//[^\n]*}{}g' \
        "$file" > "$tmp"

    if grep -n -E '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]*(^|/)io/' "$tmp" \
        | sed "s|^|FAIL io: $file:|" | grep . ; then
        iocode=1
    fi

    if grep -n -E '\bdim[[:space:]]*\*[[:space:]]*dim\b|\[[[:space:]]*dim[[:space:]]*\][[:space:]]*\[[[:space:]]*dim[[:space:]]*\]' "$tmp" \
        | sed "s|^|FAIL dense: $file:|" | grep . ; then
        densecode=2
    fi

    base=$(basename "$file")
    if [ "$base" = "evolve.c" ]; then
        # Owner: only the two default hooks may touch the heap.
        if grep -n -E "$alloc_re" "$tmp" \
            | grep -v -E ':[[:space:]]*return calloc\(count, size\);[[:space:]]*$' \
            | grep -v -E ':[[:space:]]*free\(ptr\);[[:space:]]*$' \
            | sed "s|^|FAIL alloc: $file:|" | grep . ; then
            alloccode=3
        fi
    elif grep -n -E "$alloc_re" "$tmp" | sed "s|^|FAIL alloc: $file:|" | grep . ; then
        alloccode=3
    fi
done

for c in $iocode $densecode $alloccode; do
    if [ "$c" -ne 0 ]; then
        code=$c
        break
    fi
done
if [ "$code" -eq 0 ]; then
    printf 'OK: %s: no io include, no dense matrix, no allocation outside the owner\n' "$root"
fi
exit "$code"

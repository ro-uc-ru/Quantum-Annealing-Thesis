#!/bin/sh
# Records the 002-hamiltonian test configuration (constitution §13, FR-011).
#
# Writes a deterministic, machine-readable CSV record (`v1`) with
# spec_version, N, dim, the spec §7 energy vectors, git sha, clang version,
# and CMake flags (`seed`, `schedule` and `dt_steps` are N/A: no
# evolution/sampling in this spec). No timestamps, no environment reads:
# the same build always yields the same bytes. The record lands in
# `results/` (constitution §14); the versioned source of truth is this
# script plus `CMakeLists.txt`.
#
# Usage: record-002-config.sh <compiler> <build-type> <sanitize> <c-flags> <src-dir> <output>
# Exit: 0 = record written and its contract validated, 1 = validation
#       failure, 2 = usage error.
# Owner: caller; reads the compiler version and git sha, writes one file,
# allocates nothing.

set -eu

if [ "$#" -ne 6 ]; then
    printf 'usage: %s <compiler> <build-type> <sanitize> <c-flags> <src-dir> <output>\n' "$0" >&2
    exit 2
fi

cc=$1
build_type=$2
sanitize=$3
c_flags=$4
srcdir=$5
output=$6

clang_version=$("$cc" --version 2>/dev/null | head -n 1)

if git -C "$srcdir" rev-parse HEAD >/dev/null 2>&1; then
    git_sha=$(git -C "$srcdir" rev-parse HEAD)
else
    git_sha="unavailable (not a git repository)"
fi

if [ -z "$git_sha" ]; then
    printf 'FAIL: %s: record breaks contract: empty git sha\n' "$output" >&2
    exit 1
fi
if [ -z "$clang_version" ]; then
    printf 'FAIL: %s: record breaks contract: empty clang version\n' "$output" >&2
    exit 1
fi

csv_escape() {
    printf '%s' "$1" | sed -e 's/"/""/g'
}

mkdir -p "$(dirname "$output")"

{
    printf 'spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed,schedule,dt_steps\n'
    printf 'v1,"2,3,4","16,512,65536","2:0,9,11,15;3:0,1,256,511;4:0,1,16770,32768,33825,65535","%s","%s","%s",N/A,N/A,N/A\n' \
        "$(csv_escape "$git_sha")" \
        "$(csv_escape "$clang_version")" \
        "$(csv_escape "build_type=${build_type};c_flags=${c_flags};sanitize=${sanitize};c_standard=17")"
} > "$output"

# The record is machine-readable only if its contract holds: the exact
# FR-011 header, the `v1` version, N/dim coverage 2..4, the §7 vectors
# (0x8421 = 33825, solution 16770), every §13 field, and N/A markers.
check_present() {
    if ! grep -qF "$1" "$output"; then
        printf 'FAIL: %s: record breaks contract: %s\n' "$output" "$2" >&2
        exit 1
    fi
}

check_present 'spec_version,N,dim,vectors,git_sha,clang_version,cmake_flags,seed,schedule,dt_steps' "FR-011 header"
check_present 'v1,' "version v1"
check_present '"2,3,4"' "N values"
check_present '"16,512,65536"' "dim values"
check_present '"2:0,9,11,15;3:0,1,256,511;4:0,1,16770,32768,33825,65535"' "spec §7 vectors"
check_present ',N/A,N/A,N/A' "seed/schedule/dt_steps N/A (FR-011)"

if [ "$(wc -l < "$output")" -ne 2 ]; then
    printf 'FAIL: %s: record breaks contract: expected exactly header + v1 row\n' "$output" >&2
    exit 1
fi

printf 'OK: %s: 002-hamiltonian configuration recorded\n' "$output"

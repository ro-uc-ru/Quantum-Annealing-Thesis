#!/bin/sh
# Records the 001-states test configuration (constitution §13, spec §5).
#
# Writes a deterministic, machine-readable JSON record with N, the spec §4
# grid vectors, git sha, clang version, and CMake flags (`seed`, `schedule`
# and `dt/steps` are N/A for this spec, recorded as null per spec §5).
# No timestamps, no environment reads: the same build always yields the
# same bytes. The record lands in the build tree (constitution §14); the
# versioned source of truth is this script plus `CMakeLists.txt`.
#
# Usage: record-001-config.sh <compiler> <build-type> <sanitize> <c-flags> <src-dir> <output>
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

json_escape() {
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

{
    printf '{\n'
    printf '  "spec": "001-states",\n'
    printf '  "n": [2, 3, 4],\n'
    printf '  "vectors": {"2": [9, 11], "3": [0, 511, 256, 1], "4": [0, 65535, 32768, 1, 33793]},\n'
    printf '  "seed": null,\n'
    printf '  "schedule": null,\n'
    printf '  "dt_steps": null,\n'
    printf '  "git_sha": "%s",\n' "$(json_escape "$git_sha")"
    printf '  "clang_version": "%s",\n' "$(json_escape "$clang_version")"
    printf '  "cmake_build_type": "%s",\n' "$(json_escape "$build_type")"
    printf '  "cmake_c_flags": "%s",\n' "$(json_escape "$c_flags")"
    printf '  "qa_sanitize": "%s",\n' "$(json_escape "$sanitize")"
    printf '  "cmake_c_standard": "17"\n'
    printf '}\n'
} > "$output"

# The record is machine-readable only if its contract holds: spec id,
# N, the exact §4 vectors (0x8421 = 33793), and every §13 field present.
check_present() {
    if ! grep -qF "$1" "$output"; then
        printf 'FAIL: %s: record breaks contract: %s\n' "$output" "$2" >&2
        exit 1
    fi
}

check_present '"spec": "001-states"' "spec id"
check_present '"n": [2, 3, 4]' "N values"
check_present '"vectors": {"2": [9, 11], "3": [0, 511, 256, 1], "4": [0, 65535, 32768, 1, 33793]}' "spec §4 vectors"
check_present '"seed": null' "seed field (§13, N/A per spec §5)"
check_present '"schedule": null' "schedule field (§13, N/A per spec §5)"
check_present '"dt_steps": null' "dt/steps field (§13, N/A per spec §5)"
check_present '"git_sha":' "git sha field"
check_present '"clang_version":' "clang version field"
check_present '"cmake_build_type":' "CMake build type field"
check_present '"cmake_c_flags":' "CMake flags field"
check_present '"qa_sanitize":' "sanitizer field"

printf 'OK: %s: 001-states configuration recorded\n' "$output"

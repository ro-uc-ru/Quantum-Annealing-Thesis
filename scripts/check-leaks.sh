#!/usr/bin/env bash
# Leak check of every test of a build tree (constitution MEM-2), macOS only:
# runs `leaks --atExit` over each executable registered in CTest, with the
# arguments CTest gives it, and over an optional extra command.
#
# Usage: check-leaks.sh [build-dir] [extra "executable args"]
#   build-dir  configured and built CMake tree (default: build)
#   extra      one more command to check, e.g. a CLI that is not a CTest test
# Exit: 0 = every execution reports 0 leaks, 1 = a leak or a tool error,
#       2 = usage or missing tool (leaks, jq, ctest, unbuilt tree).
# Linux has no `leaks`: use LeakSanitizer instead
#   (ASAN_OPTIONS=detect_leaks=1 ctest --test-dir <sanitizer tree>).
# Owner: caller; reads the build tree and runs its tests, which may write under
# `results/` as they do under plain `ctest`.
set -u

build="${1:-build}"
extra="${2:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || exit 2
cd "$root" || exit 2

for tool in leaks jq ctest; do
  command -v "$tool" >/dev/null 2>&1 || { printf 'check-leaks: %s not found\n' "$tool" >&2; exit 2; }
done
[ -f "$build/CMakeCache.txt" ] || { printf 'check-leaks: %s is not a configured build tree\n' "$build" >&2; exit 2; }

# Only executables built inside the tree (the header gates call the compiler).
cmds="$(ctest --test-dir "$build" --show-only=json-v1 \
  | jq -r --arg b "$root/$build/" '.tests[] | select(.command[0] | startswith($b)) | .command | @sh')"
[ -n "$extra" ] && cmds="$cmds"$'\n'"$extra"

execs=0
bad=0
while IFS= read -r c; do
  [ -n "$c" ] || continue
  execs=$((execs + 1))
  out="$(eval "leaks --atExit -- $c" 2>&1)"
  if ! printf '%s\n' "$out" | grep -q ' 0 leaks for 0 total leaked bytes'; then
    bad=$((bad + 1))
    printf 'leaks problem in: %s\n' "$c"
    printf '%s\n' "$out" | tail -n 40
  fi
done <<< "$cmds"

printf 'leaks --atExit: %d executions, %d with leaks or errors\n' "$execs" "$bad"
[ "$bad" -eq 0 ]

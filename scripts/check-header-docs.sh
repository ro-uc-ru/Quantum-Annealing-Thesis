#!/bin/sh
# Checks constitution §6 documentation on every public declaration of a
# header: purpose (@brief), inputs/outputs (@param), errors (@return),
# ownership (@owner), and numerical assumptions (@assumes).
#
# Usage: check-header-docs.sh <header-file>
# Exit: 0 = every public declaration documented, 1 = documentation failure
#       (missing header included), 2 = usage error.
# Owner: caller; reads the header, allocates nothing, writes nothing.

set -eu

if [ "$#" -ne 1 ]; then
    printf 'usage: %s <header-file>\n' "$0" >&2
    exit 2
fi

header=$1

if [ ! -f "$header" ]; then
    printf 'FAIL: %s: header does not exist\n' "$header" >&2
    exit 1
fi

awk '
function report(message) {
    printf("FAIL: %s:%d %s\n", FILENAME, currentLine, message)
    failed = 1
}

BEGIN {
    inDoc = 0
    wantDoc = 0
    block = ""
    blockLine = 0
    currentLine = 0
    declarations = 0
}

{
    currentLine = NR

    if (inDoc == 1) {
        block = block "\n" $0
        if (index($0, "*/") > 0) {
            inDoc = 0
            wantDoc = 1
        }
        next
    }

    if ($0 ~ /^[ \t]*\/\*\*/) {
        inDoc = 1
        block = $0
        blockLine = NR
        next
    }

    if ($0 ~ /^[ \t]*$/) {
        next
    }

    if ($0 ~ /^(typedef|QaStatus|void|int|char|short|long|float|double|unsigned|const|size_t|uint[0-9]+_t|struct|enum)[ \t*]/) {
        declarations = declarations + 1
        if (wantDoc == 0) {
            report("public declaration has no preceding /** doc block")
            next
        }
        if ($0 ~ /^typedef/) {
            if (index(block, "@brief") == 0) {
                report("typedef missing @brief")
            }
        } else {
            if (index(block, "@brief") == 0) {
                report("missing @brief (purpose)")
            }
            if (index(block, "@param") == 0) {
                report("missing @param (inputs/outputs)")
            }
            if (index(block, "@return") == 0) {
                report("missing @return (errors)")
            }
            if (index(block, "@owner") == 0) {
                report("missing @owner (ownership)")
            }
            if (index(block, "@assumes") == 0) {
                report("missing @assumes (numerical assumptions)")
            }
        }
        wantDoc = 0
        next
    }

    wantDoc = 0
}

END {
    if (inDoc == 1) {
        printf("FAIL: %s:%d unterminated doc block\n", FILENAME, blockLine)
        failed = 1
    }
    if (declarations == 0) {
        printf("FAIL: %s: no public declarations found\n", FILENAME)
        failed = 1
    }
    if (failed == 1) {
        exit 1
    }
    printf("OK: %s: %d public declarations documented\n", FILENAME, declarations)
}
' "$header"

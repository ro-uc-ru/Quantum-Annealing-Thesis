/*
 * 001-states scaffold smoke test (task T01).
 *
 * Purpose: prove the CMake/CTest harness builds, links `qa_core`, runs, and
 * exits 0, which is the `Hecho cuando:` condition of task T01. It asserts
 * only the platform assumptions of spec 001-states §1/§3 (a 32-bit
 * `uint32_t` behind `QaGridId`); RF-001..RF-007 behavior is tested by
 * T06-T10, not here.
 *
 * Ownership: no allocation; nothing to release. Errors: any failed check is
 * printed and makes the process exit non-zero. Numerical assumptions: none
 * beyond `uint32_t` existing with exactly 32 bits. Determinism: no clock,
 * no RNG, no environment or filesystem access, so repeated runs are
 * byte-identical (constitution §13).
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

/* Records one failing check; the run stays allocation-free. */
static int checkFailed(int condition, const char *what)
{
    if (condition) {
        return 0;
    }
    printf("FAIL: %s\n", what);
    return 1;
}

int main(void)
{
    int failures = 0;

    failures += checkFailed(sizeof (uint32_t) == 4,
                             "uint32_t must be 4 bytes (QaGridId packing)");
    failures += checkFailed(UINT32_MAX == 0xFFFFFFFFu,
                             "UINT32_MAX must equal 0xFFFFFFFF");
    failures += checkFailed(sizeof (unsigned int) * CHAR_BIT == 32u,
                             "unsigned int must be 32-bit so N == 5 is representable");
    /* The build type must survive an empty -DCMAKE_BUILD_TYPE (CMakeLists.txt
     * guards it), otherwise the recorded configuration would claim nothing. */
    failures += checkFailed(QA_BUILD_TYPE[0] != '\0',
                             "QA_BUILD_TYPE must be a non-empty recorded value");

    printf("001-states scaffold: %s, build=%s, uint32_t=%zu bits\n",
           __clang_version__, QA_BUILD_TYPE, sizeof (uint32_t) * CHAR_BIT);

    if (failures != 0) {
        printf("001-states scaffold: FAILED (%d checks)\n", failures);
        return 1;
    }
    printf("001-states scaffold: OK\n");
    return 0;
}

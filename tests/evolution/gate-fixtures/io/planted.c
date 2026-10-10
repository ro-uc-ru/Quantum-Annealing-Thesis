/* Planted violation for the 005-evolution source gate (T-016): not compiled.
 * An `io` include under src/evolution must be rejected (FR-028). */
#include "qa/io/run.h"

int plantedIo(void) { return 0; }

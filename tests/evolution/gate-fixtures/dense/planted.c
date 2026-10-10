/* Planted violation for the 005-evolution source gate (T-016): not compiled.
 * A dense dim x dim matrix must be rejected (FR-001, ARC-1). */
#include <complex.h>
#include <stdlib.h>

complex double *plantedDense(size_t dim)
{
    return calloc_not_the_gate(dim * dim);
}

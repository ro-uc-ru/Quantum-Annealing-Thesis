/* Planted violation for the 005-evolution source gate (T-016): not compiled.
 * A malloc outside the two default hooks of the owner must be rejected (FR-021). */
#include <stdlib.h>

void *plantedAlloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void *plantedExtra(size_t bytes)
{
    return malloc(bytes);
}

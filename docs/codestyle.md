# C Code Style

Normative companion to `AGENTS.md` and `docs/constitution.md`.
If this file ever contradicts either document, the constitution and
`AGENTS.md` win and this file MUST be fixed.

Goals: standard C17, readability first, explicit ownership, no leaks,
checked errors, deterministic behavior.

Style only: this file defines how code must look, never project behavior,
module names, or API shapes. All domain examples use dummy `QaFoo`
nomenclature so they cannot collide with real specs. Real names, types,
and behaviors live in the approved specs.

## 1. Naming

- Standard C. `camelCase` is preferred for functions, variables, and fields.
- Types use `Qa` prefix plus PascalCase (dummy example: `QaFoo`, `QaStatus`).
- Functions use `qa` prefix plus module plus verb (dummy: `qaFooCreate`).
- Macros and header guards are upper snake case: `QA_CORE_FOO_H`.
- Pointer out-params are named `outX`: `outFoo`, `outCount`.

## 2. Spacing and formatting

Readability is key. The project uses spaces around `->`.

- Write `f -> a`, never `f->a`.
- Spaces around `=`, `==`, `!=`, `<`, `>`, `+`, `-`, `*` (binary), `/`, `&&`, `||`.
- No spaces around `.`, `[]`, unary `*` and `&`, or after `(` / before `)`.
- Pointer star binds to the name: `Type *name`, not `Type* name`.
- `sizeof` keeps a space: `sizeof *foo`, `sizeof (size_t)`.

```c
foo -> count = count;
foo -> vals = vals;
total += vals[i] * vals[i];
```

## 3. Headers

- One public header per module under `include/qa/<module>/`.
- C17 include guards, `<stddef.h>` / `<stdint.h>` style system includes first.
- Headers never allocate; they only declare ownership.

```c
#ifndef QA_CORE_FOO_H
#define QA_CORE_FOO_H

#include <stddef.h>
#include <stdint.h>

#endif /* QA_CORE_FOO_H */
```

## 4. Structs

- One owner, one documented release path.
- Mark every owned pointer with `/* owned */`.
- Keep integer fields in `size_t` or the unsigned width the spec fixes;
  validate before shifting or multiplying.

```c
/* Dummy holder. Owner: caller of qaFooCreate. */
typedef struct QaFoo {
    size_t count;     /* element count, validated before any multiply */
    double *vals;     /* owned, never NULL when count > 0 */
} QaFoo;
```

## 5. Status enums

Use a documented `enum`, not a bare `int`, so readers, agents, and
maintainers can trace every failure without guessing `errno` values.
Each enumerator documents when it is returned and who owns what on exit.

```c
/* Function result. Every failure leaves documented ownership state. */
typedef enum QaStatus {
    QA_OK = 0,            /* Success. Out-params hold the results. */
    QA_ERR_RANGE = 1,     /* Bad size or index. Out-params untouched. */
    QA_ERR_NOMEM = 2,     /* Allocation failed. Nothing leaked. */
    QA_ERR_OVERFLOW = 3,  /* Integer shift or multiply would overflow. */
    QA_ERR_IO = 4,        /* File open, read, or write failed. */
    QA_ERR_DOMAIN = 5,    /* Numerical input outside the valid domain. */
    QA_ERR_UNSUPPORTED = 6 /* Valid input outside the staged scope (e.g. future sizes). */
} QaStatus;
```

Rules:

- Library code returns `QaStatus`; never call `exit()` in library code.
- Document every `QaStatus` a function can return.
- On failure, out-params MUST be untouched as documented (poisoned where owned).
- CLI layers may translate `QaStatus` to process exit codes.

## 6. Function documentation

Every public and non-obvious internal function documents purpose,
inputs, outputs, ownership, errors, and numerical assumptions.
Field layout follows constitution §6; specs define the real content.

Template:

```c
/**
 * @brief One-line purpose.
 *
 * @param[in]  inArg    Meaning, units, valid range.
 * @param[out] outArg   Non-NULL receiver. Set only on success unless stated.
 *
 * @return QaStatus code. Lists all reachable codes.
 *
 * @owner Ownership after QA_OK and the matching release path.
 * @assumes Preconditions, e.g. validated ranges, finite inputs.
 */
```

## 7. Functions, ownership, and const

- Create/Destroy pairs (dummy): `qaFooCreate` / `qaFooDestroy`.
- `Destroy` is idempotent and NULL-safe so `goto cleanup` stays simple.
- Read-only inputs are `const`: `QaStatus qaFooMetric(const QaFoo *foo, double *outMetric);`
- Validate out-params first and poison owned out-params (`*outFoo = NULL`) before work.
- Prefer bounds-safe arithmetic; check shifts and multiplies.
- Keep modules small and deterministic; no hidden globals or RNG without seed.

## 8. Error handling and leak prevention

Standard pattern is early validation plus `goto cleanup`.
There is exactly one success path and every failure path frees
what it owns. This keeps leak review mechanical for agents.

Header (dummy):

```c
QaStatus qaFooCreate(size_t count, QaFoo **outFoo);
void qaFooDestroy(QaFoo *foo);
```

Implementation (dummy):

```c
#include "qa/core/foo.h"

#include <stdlib.h>
#include <stdint.h>

QaStatus qaFooCreate(size_t count, QaFoo **outFoo)
{
    if (outFoo == NULL) {
        return QA_ERR_RANGE;
    }
    *outFoo = NULL;

    if (count == 0 || count > 1024) {
        return QA_ERR_RANGE;
    }
    if (count > SIZE_MAX / sizeof (double)) {
        return QA_ERR_OVERFLOW;
    }

    QaFoo *foo = malloc(sizeof *foo);
    if (foo == NULL) {
        return QA_ERR_NOMEM;
    }
    foo -> count = 0;
    foo -> vals = NULL;

    double *vals = calloc(count, sizeof *vals);
    if (vals == NULL) {
        free(foo);
        return QA_ERR_NOMEM;
    }

    vals[0] = 1.0;
    foo -> count = count;
    foo -> vals = vals;
    *outFoo = foo;
    return QA_OK;
}

void qaFooDestroy(QaFoo *foo)
{
    if (foo == NULL) {
        return;
    }
    free(foo -> vals);
    foo -> vals = NULL;
    foo -> count = 0;
    free(foo);
}
```

Multi-resource function sketch:

```c
QaStatus qaRunExample(size_t count, double **outValues)
{
    QaStatus status = QA_OK;
    double *values = NULL;

    if (outValues == NULL) {
        return QA_ERR_RANGE;
    }
    *outValues = NULL;

    values = calloc(count, sizeof *values);
    if (values == NULL) {
        status = QA_ERR_NOMEM;
        goto cleanup;
    }

    /* ... checked work that may set status ... */

    *outValues = values;
    values = NULL;
    status = QA_OK;

cleanup:
    free(values);
    return status;
}
```

Checklist for agents implementing specs:

- Poison out-params on entry.
- Initialize owned pointers to `NULL` before any fallible call.
- Null the local after handing ownership to the caller.
- Free on every `goto cleanup` path; `Destroy` stays NULL-safe.
- Never materialize dense Hamiltonian matrices; apply operators matrix-free.
- Preserve state normalization within the spec tolerance (default `1e-12`).

## 9. Anti-patterns

- No `f->a` without spaces.
- No bare `int` error codes in new library APIs; use `QaStatus`.
- No `malloc` without a NULL check and a documented owner.
- No `exit()`, no unchecked `fopen`, `fread`, `fwrite`, casts, or shifts.
- No dense Hamiltonian allocation.
- No unseeded randomness in tests or experiments.

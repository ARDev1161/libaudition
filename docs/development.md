# Development guide

## Toolchain

- C++17
- CMake >= 3.20
- GoogleTest
- Doxygen
- clang-format configuration in `.clang-format`
- clang-tidy configuration in `.clang-tidy`

## Commit convention

Commits follow Conventional Commits / Commitizen (`cz commit`) style:

- `feat(scope): ...`
- `fix(scope): ...`
- `docs(scope): ...`
- `test(scope): ...`
- `refactor(scope): ...`
- `perf(scope): ...`
- `build(scope): ...`
- `chore(scope): ...`

Each commit should be independently understandable and should not mix unrelated
refactors, behavior changes, and documentation changes.

## SOLID rules

- Prefer narrow capability interfaces over manager/god interfaces.
- Inject interfaces into orchestration code; never instantiate a concrete ML
  backend inside a generic pipeline.
- Use Adapter at third-party boundaries.
- Use Strategy for replaceable algorithms.
- Use Factory/Registry for application-selected implementations.
- Use Repository interfaces for persistent identity/model/event storage.
- Use PImpl for concrete backends whose third-party headers would otherwise leak.
- Do not introduce a pattern if a plain value type or function is clearer.

## Public headers

Third-party headers are forbidden from `include/audition/core`, domain types, and
algorithm interfaces. Backend-specific public headers may expose libaudition
configuration values but still should avoid exposing third-party implementation types.

## Thread safety documentation

Every concrete backend must document one of:

- immutable/thread-safe
- instance-confined
- session-confined
- externally synchronized

Factories/models may share immutable inference resources while creating
independent per-stream sessions.

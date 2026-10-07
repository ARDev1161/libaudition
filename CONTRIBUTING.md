# Contributing

Read `docs/specification.md`, `docs/architecture.md`, and the ADRs before changing
public interfaces. New algorithms should normally enter through an interface and
adapter unless there is intentionally only one meaningful implementation.

Use `cz commit` / Conventional Commit messages and keep commits atomic.

Before opening a change:

```bash
cmake -S . -B build -DLIBAUDITION_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Public API changes in `0.x` require updating relevant ADR/specification text and
Doxygen comments.

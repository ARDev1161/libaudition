# ADR-0009: Explicitly linked backends in v0.x

**Status:** Accepted

v0.x supports optional CMake backend targets and explicit `FactoryRegistry`
registration. Dynamic DSO plugins are deferred until a stable plugin ABI can be
designed and versioned.

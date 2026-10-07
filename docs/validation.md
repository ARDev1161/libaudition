# Validation status

## Performed for v0.1.0

The dependency-free build was configured and compiled with:

- GCC 14.2
- Clang 17
- C++17
- warnings-as-errors
- examples enabled

The produced package was installed to a clean prefix and successfully consumed by a separate
CMake project through `find_package(libacoustic CONFIG REQUIRED)` and exported targets.

## GoogleTest

The repository contains GoogleTest unit tests and CI configuration using GoogleTest 1.18.0.
The isolated artifact-build environment used to assemble v0.1.0 did not have GoogleTest
installed and did not have working outbound Git access, so the GoogleTest executable was not
run locally. GitHub CI is configured to fetch the pinned dependency and execute the suite.

## Optional spdlog backend

The spdlog adapter targets spdlog 1.17.0. The core was locally validated with the optional
adapter disabled because spdlog was not present in the isolated build image. CI builds the
normal configuration with the pinned dependency enabled.

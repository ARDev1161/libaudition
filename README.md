# libaudition

`libaudition` is a C++17 toolkit for real-time and offline acoustic perception,
spatial audio analysis, speech intelligence, source characterization, and
probabilistic acoustic observations.

The library is intentionally **application-agnostic**. It provides composable
algorithmic blocks and stable domain types; the application decides scheduling,
real-time policy, threading, recording policy, robot behavior, and middleware.

## Design rules

- Apache-2.0 project license.
- SOLID and dependency inversion throughout the public API.
- Third-party engines are adapters, never domain models.
- No ROS 2, ODAS, sherpa-onnx, ONNX Runtime, GTSAM, or other third-party types in public core APIs.
- Algorithms do not create scheduler threads implicitly.
- Configuration is expressed as typed C++ structures; YAML/JSON are serialization concerns.
- Backend implementations are replaceable through narrow interfaces.
- Public API is C++17.

See [`docs/specification.md`](docs/specification.md) and
[`docs/architecture.md`](docs/architecture.md).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Set `LIBAUDITION_FETCH_DEPENDENCIES=OFF` to require system-provided dependencies.

The direct ODAS spatial backend is opt-in:

```bash
cmake -S . -B build -DLIBAUDITION_WITH_ODAS=ON
```

See [`docs/backends/odas.md`](docs/backends/odas.md).

The audio frontend and optional resampler/AEC backends can be enabled independently:

```bash
cmake -S . -B build \
  -DLIBAUDITION_WITH_LIBSAMPLERATE=ON \
  -DLIBAUDITION_WITH_WEBRTC_AEC3=ON
```

The public API remains C++17. The pinned standalone AEC3 implementation is built
behind a shared-library boundary with C++20 internally. See
[`docs/audio_frontend.md`](docs/audio_frontend.md).

## Consume from CMake

```cmake
find_package(libaudition CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE audition::core audition::dsp audition::pipeline)
```

```cpp
#include <audition/audition.hpp>
```

## Status

Version `0.2.x` adds the first direct spatial backend. Public APIs may evolve before `1.0`.

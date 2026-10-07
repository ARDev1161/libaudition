# ADR-0012: use libodas directly for spatial audition

**Status:** accepted

## Context

The existing `audio_nav2` prototype used `odas_ros`, but `libaudition` is a
middleware-independent Apache-2.0 toolkit. `odas_ros` adds a ROS 2/socket layer
and has licensing/architecture constraints that are unnecessary inside the
library. ODAS itself is MIT-licensed and exposes the required C modules.

## Decision

- Implement `OdasSpatialEngine : ISpatialAudioEngine` directly over `libodas`.
- Keep ODAS optional behind `LIBAUDITION_WITH_ODAS`.
- Hide all ODAS types in the `.cpp` implementation using PImpl.
- Compose the ODAS STFT/SSL/SST/SSS/ISTFT modules in-process.
- Keep audio capture and scheduling outside the backend.
- Associate separated audio by ODAS fixed track slot, not active-track order.
- Preserve unknown ODAS angular uncertainty as `std::nullopt`.
- Pin the audited upstream revision when FetchContent is used.

## Consequences

The public `libaudition` domain API remains independent of ODAS and ROS 2. A
future spatial backend can replace ODAS without changing consumers of
`ISpatialAudioEngine`. Applications must provide one configured hop per call and
serialize calls to an engine instance. `reset()` reconstructs ODAS state and is
not a real-time operation.

# Library modules

`libaudition` provides the nine CMake-exported modules below. Every **module name on the homepage** links to a dedicated section here, not an unrelated guide. All targets are referenced as `audition::<module>`; the optional ML and spatial engines are separate adapters.

The **source headers** links point to the public, installable C++ declarations. The **related guide** links provide deeper context; the [complete C++ API](api.rst) and [Doxygen file/class index](https://ardev1161.github.io/libaudition/doxygen/index.html) provide per-declaration references.

## core

- **Target:** `audition::core`
- **Depends on:** no other libaudition target
- **Provides:** shared error handling, time and clock identities, PCM buffers/views and core data types
- **Public headers:** [core](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/core), [audio](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/audio)
- **Related guide:** [Public data contracts](specification.md)

## interfaces

- **Target:** `audition::interfaces` (header-only CMake INTERFACE target)
- **Depends on:** `audition::core`
- **Provides:** backend-independent abstractions used by applications, pipelines and optional engines
- **Public headers:** [interfaces](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/interfaces), [backend interfaces](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/backend)
- **Related guide:** [Backend contracts](backend_contracts.md)

## io

- **Target:** `audition::io`
- **Depends on:** `audition::core`
- **Provides:** reusable WAV I/O and recording timeline serialization, without Qt/ALSA capture
- **Public headers:** [io](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/io), [recording](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/recording)
- **Related guides:** [Recording format](recording_format.md), [Export and replay](recording_export_replay.md)

## dsp

- **Target:** `audition::dsp`
- **Depends on:** `audition::core`
- **Provides:** basic DSP, frontend transformations, signal quality and level measurements
- **Public headers:** [dsp](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/dsp)
- **Related guide:** [Audio frontend](audio_frontend.md)

## spatial

- **Target:** `audition::spatial`
- **Depends on:** `audition::interfaces`
- **Provides:** level/range priors, SPL calibration and temporal spatial smoothing; ODAS localization/separation is an **optional backend**, not this target
- **Public headers:** [spatial](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/spatial)
- **Related guides:** [SPL calibration](spl_calibration.md), [Temporal spatial smoothing](temporal_spatial_smoothing.md), [ODAS](backends/odas.md)

## pipeline

- **Target:** `audition::pipeline`
- **Depends on:** `audition::core` and `audition::interfaces`; internally also uses threads
- **Provides:** composable per-source speech/classification runtimes, acoustic event detection/assembly and spatial identity coordination
- **Public headers:** [pipeline](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/pipeline), [speech](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/speech)
- **Related guides:** [Per-source speech](per_source_speech.md), [Temporal acoustic events](temporal_acoustic_events.md)

## memory

- **Target:** `audition::memory`
- **Depends on:** `audition::interfaces`
- **Provides:** acoustic source and speaker registries, prototype matching and persistent acoustic source-identity resolution
- **Public headers:** [memory](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/memory)
- **Related guide:** [Spatial identity integration](spatial_identity_integration.md)

## model

- **Target:** `audition::model`
- **Depends on:** `audition::interfaces`
- **Provides:** in-memory model metadata registry; does **not** bundle model weights or inference runtimes
- **Public headers:** [model](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/model)
- **Related guide:** [Models](models.md)

## logging

- **Target:** `audition::logging`
- **Depends on:** `audition::core`
- **Provides:** logging integration and abstractions without a required GUI or concrete model backend
- **Public headers:** [logging](https://github.com/ARDev1161/libaudition/tree/documentation/include/audition/logging)
- **API:** [C++ API reference](api.rst)

## Optional adapters versus modules

The CMake targets above are reusable components shipped by the base SDK. ODAS, sherpa-onnx, whisper.cpp, YAMNet, CLAP and other optional backends implement selected interfaces and may require third-party dependencies and model assets. See [optional backends](backend_contracts.md) and the dedicated backend guides rather than assuming they are provided by a base module.

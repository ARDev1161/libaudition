# libaudition

**Composable C++17 acoustic perception toolkit** for spatial audio, source tracking, speech, sound classification, acoustic identity, and temporal event analysis.

The library provides reusable algorithms and acoustic observations. Applications own capture, scheduling, storage, ROS 2 integration, multimodal fusion, and actions. Optional ML backends and model weights are separate from the dependency-light core.

- [Source code](https://github.com/ARDev1161/libaudition)
- [Current release-candidate status](release_v1_rc1.md)
- [Functional architecture and algorithm map](functional_architecture.md)
- [C++ API reference](api.rst)

~~~{toctree}
:caption: Getting started
:maxdepth: 2

getting_started
cli_examples
full_demo_build
qt_demo
~~~

~~~{toctree}
:caption: Architecture and contracts
:maxdepth: 2

functional_architecture
architecture
specification
backend_contracts
audio_frontend
per_source_speech
temporal_acoustic_events
temporal_patterns
~~~

~~~{toctree}
:caption: Optional backends
:maxdepth: 1

backends/odas
backends/sherpa
backends/yamnet
backends/efficientat
backends/clap
backends/aasist
backends/world
backends/gtsam
~~~

~~~{toctree}
:caption: C++ reference
:maxdepth: 2

api
~~~

~~~{toctree}
:caption: Development and releases
:maxdepth: 1

development
development_roadmap
models
test_maintenance_map
ctest_suites
algorithm_test_matrix
qt_model_validation_matrix
release_v1_rc1
release_v1_acceptance
site_build
~~~

The website is built from the source documentation in this repository. API declarations are generated from public headers in the same commit; check the release notes and backend requirements before assuming an optional model or device has been validated.

# libaudition v0.1 specification

## Mission

`libaudition` is an application-agnostic C++17 library of composable acoustic
algorithms and domain types. It answers questions such as what is audible, who
or what produced it, where it came from, what was said, what characteristics
are observable, and with what uncertainty.

It deliberately does **not** decide robot behavior, navigation policy, PDDL
state, task relevance, or UI/storage policy. It also does not own camera, radar,
odometry/TF, or other non-audio sensor processing and does not perform
cross-modal sensor fusion. A consuming application such as `audio_nav2` may
combine libaudition acoustic outputs with those systems.

## Architectural invariants

1. Domain abstractions depend only on the C++ standard library and libaudition core.
2. Third-party engines are adapters behind narrow interfaces.
3. No third-party type appears in stable core public APIs.
4. Scheduling and real-time policy belong to the application. Algorithms expose
   synchronous blocks and explicit streaming sessions; the application decides
   whether to execute them inline, on workers, in ROS executors, or offline.
5. No algorithm creates scheduler threads implicitly. A backend may create an
   internal thread only when that behavior is explicit in its configuration and
   contract.
6. Buffers are zero-copy friendly: `AudioBuffer` owns samples and `AudioView`
   borrows them.
7. Typed C++ configuration is authoritative. YAML/JSON/protobuf are adapters.
8. Transient spatial track identity is distinct from persistent acoustic-source
   identity and speaker identity.
9. Uncertainty is preserved in its native representation rather than flattened
   into a generic confidence number.
10. Core/default components remain commercially usable under permissive licenses.
11. Spatial inference inside libaudition is acoustic-only. Non-audio sensor
    observations and multimodal association/fusion belong to the consuming
    application.

## Audio contract

Internal processing uses normalized `float32` PCM. An `AudioFormat` explicitly
contains sample rate, channel count, and planar/interleaved layout. `AudioView`
never owns storage. The timestamp denotes the capture time of the first frame.

Backends declare audio requirements. Conversion, resampling, and channel mapping
must be explicit pipeline blocks; they are never silently hidden in the domain model.

## Time contract

Time is represented by `Timestamp { nanoseconds, ClockIdentity }`.

Clock domains:

- `SystemUtc`: Unix/UTC compatible wall clock.
- `Monotonic`: steady process/platform clock for latency and ordering.
- `Simulation`: application/middleware-controlled simulated time.
- `External`: independent hardware/device time; `source_id` identifies the clock.

Timestamps from different clocks are not directly subtractable. Clock
synchronization is an integration responsibility and should produce a timestamp
in a common clock domain before geometric fusion.

`google.protobuf.Timestamp` is a serialization option for `SystemUtc`, not the
internal time abstraction, because it represents UTC epoch time and cannot
express monotonic or simulated clocks.

## Coordinate contract

The canonical frame is right-handed and follows the ROS REP-103 convention:

- +X forward
- +Y left
- +Z up
- meters, radians, seconds, hertz

Backends convert their native conventions at the adapter boundary.

## Probability and uncertainty

Core provides small dependency-free primitives:

- `Probability`: calibrated probability in [0, 1]
- `Score`: uncalibrated model/backend score
- `Gaussian1D`: mean + variance
- 3D covariance for spatial estimates

Embedding quality is optional. Absence means that the embedding backend did not
provide a meaningful quality estimate; it must not be represented as probability
zero. Cosine similarity between semantic embeddings is a `Score`, not a
`Probability`.

Open-vocabulary classifiers may convert a set of model logits/similarities into
candidate-relative probabilities only when that transformation is explicit.
Such probabilities describe the supplied candidate set and must not be presented
as calibrated real-world event probabilities.

Authenticity backends follow the same rule. Binary anti-spoofing model logits are
reported as `Score`. A `Probability` is populated only when the backend natively
provides a calibrated posterior or the application supplies an explicit
calibration transform. A generic spoof detector must not fabricate replay or
synthetic-attribution probabilities when its model does not distinguish those
attack classes.

Voice-trait backends likewise report only quantities they actually estimate. A
signal-processing backend that measures F0 may populate pitch statistics while
leaving age, categorical traits, and speaking rate absent. Unvoiced or
insufficient audio is represented by absent optional pitch fields rather than
zero hertz.

Frame-level acoustic analysis is a separate contract from semantic voice traits.
`VoiceAcousticFeatures` may expose measured time/F0 contours, an explicit
per-frame voiced mask, spectral envelopes and aperiodicity matrices without
implying age, emotion, identity or another human-level inference. The voiced
mask prevents downstream code from depending on a backend-specific unvoiced-F0
sentinel. Spectral and aperiodicity matrices use row-major
`frame * frequency_bin_count + bin` layout and carry their frame period, FFT
size, sample rate and dimensions explicitly.

The core intentionally does not depend on a probability framework. Heavy
inference/fusion implementations may use Eigen, GTSAM, or another library behind
an adapter. This keeps the domain layer lightweight while preserving covariance
and posterior information required by downstream applications.

## Identity model

- `SpatialTrackId`: temporary identity issued by a spatial tracker such as ODAS.
- `AcousticSourceId`: persistent hypothesis that multiple observations/tracks
  belong to the same physical acoustic source. It is not assumed to be a person.
- `SpeakerId`: persistent vocal identity inferred from speech.

A source-identity resolver may combine continuity, position, bearing, time, and
one or more acoustic fingerprints. A single physical source can therefore retain
its `AcousticSourceId` when its spatial tracker ID changes.

The default in-memory identity resolver treats an active tracker binding as
authoritative until `endTrack()`. Re-identification after a tracker handoff is
conservative: it requires compatible persistent fingerprint evidence or a recent
world/canonical position; recency alone is never sufficient. Direction and range
may refine an already anchored candidate but do not establish identity by
themselves because the identity observation contract does not carry a sensor pose
for cross-frame direction comparison. A source already bound to another
active track is excluded from association candidates.

Fingerprint matching is isolated by `model_id` and embedding dimension. High
quality compatible fingerprints below the configured cosine threshold act as
conflicting evidence for that candidate. Low-quality fingerprints can be used by
the caller for other purposes but are not persisted or used for source re-ID by
the default resolver.

The resolver's `SourceIdentityDecision::confidence` is a bounded association
score produced by the configured evidence policy. It is not a calibrated
probability that two observations originate from the same physical object.

## Lifecycle

libaudition does not implement a middleware lifecycle. Configuration happens at
construction/factory time. Stateful algorithms expose only domain-meaningful
operations such as `reset`, `flush`, `accept`, `partial`, and `finalize`.

ROS 2 integration maps naturally to this model:

- create/configure lib objects in `on_configure`
- use them while the node is active
- stop external scheduling in `on_deactivate`
- destroy objects in `on_cleanup`

## Backend discovery

v0.x uses explicitly linked backends plus typed `FactoryRegistry` objects.
Applications may register their own factories. Dynamic shared-object discovery
is intentionally postponed until a stable plugin ABI can be designed.

## Threading and backpressure

Algorithm interfaces are synchronous unless explicitly named streaming/session.
The library provides cancellation primitives and bounded queues, but the
application owns worker threads and scheduling.

Supported queue policies:

- block producer
- drop oldest
- drop newest
- keep latest
- reject

No unbounded queue is part of the standard pipeline infrastructure.

## Execution target

Execution hardware is generic. `ExecutionTarget` expresses a device class,
optional provider name, device index, precision preference, provider options,
and fallback policy. Backends interpret provider names such as CUDA, RKNN, QNN,
OpenVINO, CoreML, TensorRT, or future accelerators without changing domain APIs.

## Logging

Core logging uses `ILogSink`. A synchronous spdlog adapter is supplied as the
first default implementation. No spdlog type leaks through the interface, so a
ROS adapter can later forward logs to `rclcpp` without changing algorithms.

## Testing

- GoogleTest for unit and contract tests.
- Backend contract suites must be reusable by every implementation of an interface.
- Recorded scenario tests compare algorithm revisions on identical data.
- Hardware-in-loop tests are separate from deterministic library unit tests.

## API stability

Only installed headers under `include/audition/` form the public API.
Version `0.x` may make breaking API changes. `1.0` will define the first stable
compatibility contract.

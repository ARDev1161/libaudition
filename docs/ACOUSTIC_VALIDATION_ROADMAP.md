# Acoustic validation roadmap — Acoustix → ODAS → libaudition

**Status:** proposed quantitative gates (to freeze before optimization), 2026-11-07. **Authority:** this is the
canonical cross-project acoustic test-and-fix roadmap. Keep `docs/acoustix_room_ci.md`
as the *how to run the existing fixture*, not the definition of product readiness.

**Mission:** establish an acoustically trustworthy, reproducible test bench
and then fix source tracking, actual per-source audio leakage, quiet speech
and ASR in libaudition without hiding errors or tuning to one scene.

## 0. What is already proved (do not overclaim)

| Item | Evidence / current state |
| --- | --- |
| Simulated 7 × 7 × 2.8 m room, 0.36 s RT60, 4 planar mics moving around seven synthetic voices + fridge/AC/car | Implemented; full 12 s render with RIR tails; **not calibrated to a measured room** |
| 16 kHz ODAS input, source stems and geometry oracle | Implemented; six input channels = dry ref, four microphone channels, silent ref |
| PCM16 source-stem reconstruction | Verified: maximum 7 LSB reconstruction residual, ≤12 LSB permitted from quantization |
| Full 12-second Acoustix ODAS replay | PASS: no recorder drops, SSL proposals, complete audio, reproducible artifacts |
| Upstream SST with SSL=7/8 | Failed: zero confirmed tracks |
| Fork `ARDev1161/odas dev` with factorized assignment + local epsilon | Full Acoustix experiment PASS: SSL counts 4..8 all produce 2,448 track observations; 1,462 one-to-one oracle source-hop matches out of 7,814 (18.71%); ≤3 simultaneous tracks; matched azimuth median ≈11.8°, P90 ≈24.4° |
| Separate CTest `audio_integration_near_plane_moving` on the fork-dev pin | **OPEN / failing**: `Unexpected WAV recorder drops under offline replay` in [libaudition CI #42340254801](https://github.com/ARDev1161/libaudition/actions/runs/42340254801); distinguish this from the successful Acoustix test. Investigate recorder scheduling/capacity and source lifecycle before L1 passes. |
| Actual speech, separation, ASR WER, physical microphone calibration | **Not validated by synthetic voiced-like signal** |

The ODAS numbers are **diagnostic counts**, not a demonstration that
three people have been correctly identified or that seven speech streams
have been separated. A directional track, an acoustic source, a persistent
speaker identity, a waveform and a transcript are distinct things.

## 1. Definition of done: three different kinds of "ready"

**Simulator / test bench readiness:**

- **S0 — deterministic structural harness: DONE.** Scenario validation,
  oracle, PCM contracts, source stems, replay, independent test assertions,
  failure artifacts. Suitable for preventing basic regressions, NOT for
  claiming physical accuracy or speech quality.
- **S1 — trusted acoustic/geometry fixture: NOT DONE.** All S1 checks below
  pass for *stationary* and moving cases, with validated sample/timestamp
  alignment, direct/RIR/RT60 checks, repeatability and isolation controls.
- **S2 — speech benchmark ready: NOT DONE.** Licensed **intelligible human**
  recordings, transcripts, true speaker IDs, clean isolated references,
  overlap/echo/quiet scenarios, reference-based leakage and WER metrics,
  held-out evaluation scenes, reproducible frozen manifests.
- **S3 — hardware-correlated simulator: NOT DONE.** Validate at least one
  microphone-array/room setup using measured impulse responses and speech
  playback. Establish error envelopes between generated and measured
  conditions. This is a separate, stronger claim than "test bench ready".

**Call the simulator "ready for fixing libaudition from its data" only
after S1 + S2 PASS.** S3 is required before claiming predictive real-room
quality, not before writing/regression-testing software.

**Call libaudition "fixed on validated simulator data" only when L1–L5
below PASS on the **held-out** speech suite and no previous CI regression.
Even then, say "synthetic-benchmark validated", not "field validated".
For real deployment quality require S3 + L6. The exact issue "SST births
disappear with ≥7 SSL candidates" is already fixed in fork dev, but the
whole libaudition spatial speech pipeline is **not** fixed.

## 2. Test matrix and ground truth

Each scenario declares a unique scenario ID, acoustic seed, source count,
geometry and motion, per-source input file/hash, SNR or target/interferer
ratio, RT60, mic geometry, calibration and the expected speech times.

| ID | Scene | Key assertion / purpose | Priority |
| --- | --- | --- | --- |
| T00 | Silence; synthetic + measured noise-only | No invented speech, no unbounded tracks, deterministic retirement | P0 |
| T01 | Impulse / one stationary dry source, reflection-free | Direct-path delay, attenuation, channel order, DOA sign and coordinates | P0 |
| T02 | One stationary actual voice, quiet and normal | VAD onset/end, transcription without clipping first word, source capture | P0 |
| T03 | Two actual voices at 30°, 60° and 120° separations; staggered/overlap | Unique assignments, separation, leakage, per-speaker ASR | P0 |
| T04 | Near user speaking while distant loudspeaker plays another speaker's voice | Reproduce user's mixing complaint; echo vs independent source; attribution | P0 |
| T05 | Four-mic robot translates and rotates while source stays fixed | World ↔ robot-frame oracle, angle over time, ID persistence, RIR transitions | P0 |
| T06 | Two/three voices plus diffuse fridge/AC/street noise | Background leakage, SNR robustness, false-track rate | P1 |
| T07 | Seven sources around table with different REAL voices | **Stress/characterization**, do not assume 4-mic array resolves seven | P1 |
| T08 | Sudden source start/stop, 3 seconds of silence, reacquisition, ID reuse | VAD pre-roll, stop/retirement timing, stale ASR guard and reset | P0 |
| T09 | Recorded ReSpeaker/reference microphone array in a measured room | Simulation-to-hardware correlation and external validity | P2 |

Use ≥3 fixed public-in-repo seeds for fast regression. Freeze a separate
evaluation manifest with ≥20 held-out seeds, containing 1/2/3 talker
cases, stationary and motion, RT60 ≈0.2/0.36/0.7 s and SNR
0/+6/+12 dB (where the physics supports them). Evaluate alternate source
angles, distances and speakers. The seven-talker case must be reported
but is **not** the acceptance criterion for ordinary two-person use.
Never optimize on the held-out test manifest.

For speech, start with ≥7 distinct human speakers, ≥3 licensed utterances
per speaker, known verbatim transcript and isolated 16-kHz mono masters.
Use one utterance per speaker for development and reserve the others for
evaluation. Match language to available ASR backends. Check redistribution
rights and speaker-consent/license terms. If speech cannot be redistributed,
store manifest, hashes and acquisition instructions; never commit private WAVs.
A "voice-like oscillator" is never a substitute in ASR or separation gates.

## 3. S1 — physical and signal-contract gates

| Gate | Required PASS evidence |
| --- | --- |
| S1.1 Repeatability | Same seed + manifest + pinned Acoustix/ODAS/tool versions → byte-identical WAV, oracle and derived metrics on the reference Linux runner; cross-platform differences captured separately |
| S1.2 Source accounting | For every mic sample, sum of individual rendered stems matches the mixed PCM16 signal within a documented quantization bound; no clipping; no hidden per-channel normalization; silent source stays silent |
| S1.3 Sample clock | Fixture length, source on/off, 128-frame hop IDs, robot pose and returned timestamps match to ≤1 source sample for purely digital timing; acoustic propagation delays are separately modelled |
| S1.4 Static physics | Anechoic direct-path arrival agrees with distance/speed-of-sound within ±2 samples; direct-path energy falls approximately 6 dB on doubling distance (tolerance ±2 dB in the no-reflection test) |
| S1.5 RIR | Direct path precedes reflections; nonzero reverb tail; measured simulated decay is within ±25% of requested RT60 on stationary fixtures where decay estimation is well defined |
| S1.6 Coordinates | Known bearing at 0/±90/180°, rotation, translation and elevation align with robot-frame oracle within 1° geometric computation error, including wraparound |
| S1.7 Motion | RIR snapshot transitions do not produce silent gaps or unexplained sample discontinuities; report approximation error when comparing static and moving-reference scenes |
| S1.8 Negative tests | Invalid geometry, duplicate IDs, corrupt/truncated stems, stereo mismatch, zero/clipped channels, mismatched durations and invalid WAV formats fail clearly |
| S1.9 Limitations | State explicitly what is NOT modelled: calibrated wall absorption, diffraction, glass transmission, true continuous Doppler/phase, mic frequency response or physical AGC/AEC |

**Do not "pass" a physical check by widening tolerance without a
reference measurement and rationale.** If an effect is outside model
scope, mark it *unsupported*, not "verified".

## 4. S2 — speech benchmark and metric validity gates

For each rendered run archive: the input six-channel WAV, 4-channel
per-source stems, isolated clean dry voice inputs, exact transcript,
speaker ID (oracle only), speaking intervals, source positions,
per-hop DOA, robot pose, mic geometry, latency parameters, versions
and exact seed. Save `manifest.json`, `ground_truth.json`,
`signal_audit.json`, `metrics.json` and `metrics_by_speaker.json`;
include separated track recordings, VAD/ASR events and diagnostics.

Before applying quality thresholds:
- Validate track ↔ source association **one-to-one** per hop, including
  missed sources, unmatched tracks, ID switches and fragmentations.
- Keep 3D angle and azimuth separate; report median/P90 **only on
  matches**, *plus* recall/precision so missed sources cannot disappear.
- For waveform separation, account for **one fixed documented delay**
  per output, compare with appropriately aligned dry/convolved target
  references, and report permutation-invariant SI-SDR improvement
  (SI-SDRi), SIR improvement (SIRi), speech preservation and the fraction
  of interferer energy in each output. Silence is not "good separation".
- Show macro-average and per-speaker results, P10/P50/P90,
  and false positives on speech-free intervals.
- WER/CER uses actual human transcripts, the same pinned ASR backend,
  model, decoding parameters and language in control/treatment;
  report deletions separately and first-word truncation.
- Compute directional leakage and ASR results for the **speaker + playback**
  scenario (T04) as an independent acceptance case, not only global mean.
- Add unit tests using constructed signals with *known exact*
  correct/incorrect leakage and permutations, so metric bugs cannot
  manufacture improvements.

**S2 is complete when** corpus, oracle, reference audio and metrics are
reproducible with documented uncertainty, all speech scenarios have
meaningful nonempty artifacts, and ≥20 held-out seeds run end-to-end.
S2 does NOT require that ODAS performs well; it establishes a credible
measuring instrument.

## 5. L1–L6 — fixing libaudition with the validated benchmark

These are proposed **acceptance targets to freeze before optimization**.
If the reference bench shows a target physically infeasible for the
4-mic array, explicitly declare the capability limit and revise the
product scope via review; do not quietly lower the threshold for a green CI.

| Gate | Initial quantitative acceptance target (held-out unless noted) |
| --- | --- |
| L1 Pipeline integrity | No dropped/duplicated input frames or source-recording corruption in offline tests; correct timestamps and stable replay; reset/flush/retirement tested; no ASan/UBSan findings |
| L2 One speaker + quiet speech | VAD onset P90 ≤150 ms from oracle speech onset with preserved pre-roll; first-word deletions ≤5% of reference utterances; no WER regression >2 **absolute percentage points** against same-ASR clean non-overlap baseline |
| L3 Two-speaker spatial tracking | On resolvable ≥60° stationary/moving cases: oracle source-hop recall ≥80%, matched azimuth median ≤15°, P90 ≤30°, track IDF1 ≥0.80; log unmatched tracks/ID switches |
| L4 Two-speaker separation + echo/playback | On ≥60° speech-overlap and T04: median SIRi ≥6 dB, median SI-SDRi ≥3 dB; P10 SIRi ≥0 dB; per-speaker WER improves ≥20% relative to same raw-mixture control or documents ASR floor limitation; all separated outputs preserve target speech rather than going silent |
| L5 Event lifecycle + robustness | After configurable 3.0 s continuous source silence, stop/retire within one 128-frame hop plus documented scheduling tolerance; source IDs not reused across active recordings/ASR; no uncontrolled recording/track growth; 60 min looped replay/soak without crash, dropped frames or increasing retained buffers |
| L6 Real-room correlation | Real array test reproduces at least single/dual-speaker T02–T05 results within a predeclared uncertainty envelope; annotate array orientation, mic gains, noise/echo path and physical mic limitations |

For L2–L4 report separately: quiet vs loud, stationary vs moving,
RT60 and SNR slices, each speaker and playback source; do **not**
accept only macro-average when one speaker is consistently lost.
The L4 WER comparison is per actual target transcript; a single mixed
transcript is not a valid substitute. New audio metrics must identify
possible ASR or source association leakage.

**Release rule:** mark "libaudition fixed on simulated room data"
only after S1, S2, L1–L5 are PASS on held-out data, all known
regressions remain green, and a reviewer signs a dated results report.
Mark "field validated" only after L6 (S3). A targeted fixed SST bug
can be marked DONE without marking all of L3/L4 DONE.

## 6. Implementation order (task-sized PRs)

| Phase | Scope / code owner | Deliverable and exit condition |
| --- | --- | --- |
| P0 — baseline inventory | libaudition test docs | Freeze current results, exact ODAS dev SHA and Acoustix SHA, source/stem/metrics contracts; mark current capabilities honestly. **Existing structural baseline DONE; this roadmap formalizes it.** |
| P1 — simulator credibility | libaudition `tests/integration` | T00/T01/T05 physics and timing oracles, deterministic repeat tests, RIR/RT60 and motion continuity checks → **S1** |
| P2 — human speech + metrics | libaudition test tooling only | Licensed corpus, reproducible T02/T03/T04/T08/T07, target/ref alignment, leakage/SIR/SI-SDR/ASR metric self-tests → **S2** |
| P3 — ODAS algorithm | `ARDev1161/odas` per-task feature branches → `dev` | Track birth numerical stability (already fixed), next evaluate source capacity, false positives, track ID persistence; each change backed by Acoustix, independent ODAS unit tests and performance checks |
| P4 — libaudition recording/VAD/ASR | libaudition `src/`, `backends/`, demo only as a consumer | Fix truncated first words, 3 s silent-source retirement, track/audio ownership and playback contamination → **L1/L2/L5** |
| P5 — separation quality | ODAS SSS or other backend in its owning repo; libaudition integration | Improve T03/T04 contamination without deleting target speech; compare before/after by held-out SI-SDRi/SIRi/WER → **L3/L4** |
| P6 — physical validation | libaudition + microphone hardware | Capture reference room/array, compare acoustic fingerprints, DOA, leakage, ASR and resource bounds → **S3/L6** |

Changes to ODAS are **not** implemented by copying DSP algorithms into
libaudition or audition_ros2. ODAS workflow: branch from
`ARDev1161/odas:dev`, dedicated fix/feat PR, own CTest/CI,
test in libaudition via a **temporary SHA-pinned experimental branch**,
merge tested fix to ODAS `dev`, then update a libaudition dependency
pin by separate PR. Never pin a floating `dev` branch and never change
ODAS `master` or libaudition `main` without an explicit merge decision.

### S1 task #1 — geometry and genuine stationary RIR (implementation PR)

The first isolated S1 task adds two CTest gates:

- `audio_acoustix_physics_selftest` (stdlib, every CI): mic rigid
  transform, oracle azimuth/elevation, yaw wrap, trajectory interpolation
  and render-window partition of unity.
- `audio_acoustix_rir_physics` (optional Acoustix CPU CI): one
  stationary impulse observed at **1 m and 2 m**, compare *relative*
  arrival time against distance / 343 m/s (within 3 samples), direct
  amplitude ratio against inverse distance (6.02 ±2.5 dB), nonzero
  reverberant tail and bit-identical repeat impulse responses.

The backend may add a fixed fractional-delay FIR group delay.
This task tests **relative** propagation time; absolute delay
calibration and direct-path reflection-free RT60 tests remain open.
The test publishes `acoustix_physics.json` for falsifiable results.
**Verified on exact branch commit `8ed6f0179bc61c5b386176cdea801c9789ecb6b3`:**
[full CPU Acoustix + CTest PASS #42343220326](https://github.com/ARDev1161/libaudition/actions/runs/42343220326).
Observed 1 m / 2 m direct RIR peaks at samples 87 / 133
(**46 samples** vs **46.647 expected**), near/far direct-peak
difference **5.435 dB** vs **6.021 dB expected**, nonzero reflected
energy and byte-identical repeat RIR. The fractional-delay FIR
introduces an approximately constant ~40-sample group delay, so this
first test gates *relative*, not absolute, propagation timing.
The old 12-second Acoustix metrics remain 2,448 tracked observations,
18.71% matched voice-source hops and zero recorder drops.
The gate does not validate moving-phase physics, acoustic materials,
ASR quality or the whole S1 milestone.

## 7. CI policy, artifacts and anti-overfitting

- **Every PR:** fast stdlib scenario/metric unit tests, C++ build,
  ODAS targeted unit tests and installed SDK tests. This includes
  negative tests and known SST regressions.
- **Acoustix ready-for-review/manual:** current full 12-second suite
  plus staged additions; publish one CI summary table per scenario
  and commit-scoped artifacts even on failure. A CI PASS requires
  proper artifacts and structural gates; never silently map
  "metrics absent" to success.
- **Release-candidate manual qualification:** full frozen held-out
  suite (≥20 seeds and real speech) and 60-minute replay, with
  paired same-source baseline and candidate SHA, versioned thresholds,
  confidence intervals/uncertainty and per-speaker failure examples.
  Keep expensive evaluation separate from a basic PR build.
- Preserve both **input manifest and output evidence**: ODAS/Acoustix
  commit hashes, libaudition commit, config, seeds, reference audio
  SHA-256, metric script versions, timestamps and environment CPU.
- Compare paired runs only when the **same input WAV and settings**
  are used. New acoustic scenes and real voices must use frozen splits.
  No target leakage from oracle into the runtime pipeline.
- A proposed quality threshold is not an enforceable gate until the
  metric has a working, independently tested implementation and the
  acceptance baseline is frozen in a reviewed PR. Keep prospective
  criteria visible even while instrumentation is being built.

## 8. Working dashboard / next checks

| Milestone | Current status | Next concrete PR |
| --- | --- | --- |
| S0 signal + headless structural contract | **PASS** | Maintain existing artifact and regression coverage |
| ODAS global-epsilon / 7–8 SSL suppression | **FIXED in fork dev**; libaudition stable pin in PR #74 | Keep exact SHA pin and verify 4/5/6/7/8 capacities |
| S1 physics and timestamps | **IN PROGRESS (S1.4/S1.6 subset under CI)** | CPU direct-path 1m/2m delay/attenuation, known yaw/mic body-frame oracle and repeat RIR; still need RT60 decay, continuous motion limits and timing boundaries |
| S2 real speech benchmark | **OPEN** | Licensed 7-speaker reference set and leakage/ASR metric contracts |
| L1 recorder integrity — near-plane moving | **BLOCKED / failing on fork pin** | Reproduce exact fixture, inspect drop counters and writer queue ownership, fix without relaxing no-drop test |
| L1/L2 first word and quiet speech | **OPEN** | VAD pre-roll with onset ground truth; same-model WER |
| L3/L4 source quality and playback mixing | **OPEN** | Two-voice overlap first, then T04 loudspeaker+user, then 3/7 stress |
| L5 3-second silence and lifecycle | **OPEN** | Track retirement and ID/recording/ASR isolation tests |
| S3/L6 measured microphone correlation | **OPEN** | ReSpeaker room recording and mismatch report |

**Revision policy:** update this dashboard and its evidence links after
every qualifying PR; record an explicit decision when a target changes.
"No new regression" is necessary but not sufficient for "audio fixed".

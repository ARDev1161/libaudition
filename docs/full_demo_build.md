# Full Qt Creator development build

Use this configuration to compile all software backends and explore the Qt
playground with ReSpeaker USB capture. The developer and CI presets remain
intentionally lean: enabling *all* optional backends by default in the root
`CMakeLists.txt` would break minimal, offline, package-consumer and
platform-specific builds.

## Presets

| Preset | Purpose |
|---|---|
| `full-demo` | Qt demo, examples, tests, Doxygen, ODAS, Sherpa+TTS, YAMNet, EfficientAT, AASIST, CLAP, WORLD, WebRTC AEC3, libsamplerate, spdlog. Downloads YAMNet/Silero/SenseVoice demo assets at configure time. |
| `full-demo-no-model-download` | Same compiled software capabilities but does not download *demo model weights*. Enter local model paths manually. It may still download missing **source/build dependencies** because `LIBAUDITION_FETCH_DEPENDENCIES=ON`. |

Both presets enable `LIBAUDITION_WARNINGS_AS_ERRORS=ON`, build all C++17
examples and GoogleTest suites, and use `RelWithDebInfo` for usable
backtraces and optimizations. They deliberately keep **GTSAM OFF**
(install a compatible GTSAM 4.2 development package first) and all
Sherpa platform-specific GPU/NPU providers OFF. Enabling a provider
requires the matching runtime/SDK; enabling all accelerator flags at once
is not a meaningful build profile.

Doxygen is required because full-demo enables
`LIBAUDITION_BUILD_DOCS=ON`. Qt Widgets, Concurrent and (recommended)
Multimedia must be installed. On Linux the ODAS/ALSA capture adapter needs
ALSA development headers; ODAS itself also needs FFTW, libconfig and
PulseAudio development dependencies.

### Terminal

```sh
cmake --preset full-demo
cmake --build --preset full-demo
ctest --preset full-demo
./build/full-demo/demo/qt/libaudition_qt_demo
```

For manual model selection / restricted model networks:

```sh
cmake --preset full-demo-no-model-download
cmake --build --preset full-demo-no-model-download
ctest --preset full-demo-no-model-download
./build/full-demo-no-model-download/demo/qt/libaudition_qt_demo
```

The build presets bound parallelism to four jobs to reduce peak RAM.
These commands do not install any models as part of the library or
authorize redistribution of third-party model artifacts.

### Qt Creator

1. Open the repository root `CMakeLists.txt` using a Linux **Desktop Qt 6**
   kit. Choose `full-demo` from the CMake presets / configure options, if
   supported by your Qt Creator version. Use the build directory
   `build/full-demo` and the Ninja generator.
2. If Qt Creator does not expose CMake presets, run `cmake --preset
   full-demo` in a terminal, then open/use that existing build directory
   in Qt Creator. Otherwise copy the preset's `cacheVariables` into
   **Projects → Build Settings → CMake Configuration → Batch Edit**.
3. **Run CMake**, then build the `all` target; select the
   `libaudition_qt_demo` executable in Run Settings.
4. For a reproducible smoke, launch it with `--self-test` and
   `QT_QPA_PLATFORM=offscreen`; the output must include
   `qt-demo-self-test=ok`.
5. With the ReSpeaker connected, open **Acoustic scene**, refresh ALSA
   devices, select the correct `hw:N,M`, six channels at 16 kHz and
   zero-based ODAS input channels `1,2,3,4`. Verify actual microphone
   geometry and angular orientation before interpreting DOA accuracy.
   See [Qt demo](qt_demo.md).

**Important cache rule:** editing `option(... ON)` in root
`CMakeLists.txt` changes only defaults for a *new* build directory.
Existing `CMakeCache.txt` entries override those defaults. Use a new
preset directory or explicitly change cached options. If you modified
the repository's root `CMakeLists.txt` locally only to enable options,
revert **only those edits** (after reviewing `git diff CMakeLists.txt`)
before pulling this PR; the `full-demo` preset replaces that approach.

### External prerequisites and known limits

- CLAP, AASIST, EfficientAT, Whisper, Piper and other optional model
  runners still need externally provisioned matching weights. Enabling
  their CMake backend is not the same as successfully running inference.
- GTSAM is independently compiled/tested in CI, but is not selected in
  `full-demo` until installed locally. Once available, add
  `-DLIBAUDITION_WITH_GTSAM=ON` to the preset configuration and ensure
  CMake can resolve the upstream GTSAM 4.2 package.
- External ONNX Runtime auto-fetching is supported on Linux x86_64 only.
  Other platforms must provide `LIBAUDITION_ONNXRUNTIME_ROOT`.
- `LIBAUDITION_REGISTER_REAL_QT_MODEL_TESTS` stays OFF; these tests
  require separately provisioned and verified model fixture environment
  variables. The ordinary `qt_self_test` does **not** require weights.
- Some model-weight licenses, SenseVoice provenance and optional AEC3
  redistribution/patent-file checks remain unresolved; see
  [third-party notices](../THIRD_PARTY.md). This is a *local development
  build*, not a redistributable binary/model bundle.

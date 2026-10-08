# Third-party policy

The `libaudition` core is Apache-2.0 and must remain usable in open-source and
commercial proprietary applications.

Rules:

1. Core/default backends must not introduce GPL, AGPL, non-commercial, or
   source-available-only obligations into the library distribution.
2. Every model artifact must have an explicit source, revision, cryptographic
   hash, and license entry in `models/manifest.yaml` before it is distributed.
3. Third-party types must not appear in public domain interfaces.
4. Research-only integrations must be opt-in and clearly separated from the
   default distribution.

The optional direct ODAS backend uses `introlab/odas` under the MIT license.
The ExternalProject source is pinned to commit
`bcb845434495e293df3d48f1203b7a86e1852449`; the upstream license text is
retained in `LICENSES/ODAS.txt`. `odas_ros` is not a dependency.

The optional resampling backend uses `libsndfile/libsamplerate` 0.2.2 under
the BSD-2-Clause license, pinned to commit
`c96f5e3de9c4488f4e6c97f59f5245f22fda22f7`. The license text is retained
in `LICENSES/libsamplerate.txt`.

The optional AEC backend currently uses the standalone `Enaium/webrtc-aec3`
extraction pinned to commit `2cec2f52e26646f93bd2d5498bbabf59cba18da9`.
Its repository root declares BSD-3-Clause in `LICENSE`. Copied WebRTC source
headers also reference an upstream `PATENTS` file, while the extraction root
does not contain that file. For that reason the adapter is opt-in and this
exact extraction/provenance must receive a final redistribution/patent-file
audit before it is treated as a default binary-distribution dependency.

The optional speech backend uses `k2-fsa/sherpa-onnx` under Apache-2.0,
pinned to commit `99ddefaa92129858b80a71a426903dd4215c83fa`. The Apache-2.0
license text is retained separately in `LICENSES/sherpa-onnx.txt`. The adapter
uses the upstream C++ wrapper over its C API and does not expose sherpa types
through libaudition public interfaces.

Sherpa model packages are **not** licensed by the sherpa-onnx code license merely
because the runtime is Apache-2.0. Model artifacts remain subject to the normal
`models/manifest.yaml` provenance/license/hash policy and are not bundled by
this backend.

The optional CLAP backend uses Microsoft ONNX Runtime under the MIT license.
Its private tokenizer JSON parsing uses nlohmann/json 3.11.3 under the MIT
license; the retained license text is `LICENSES/nlohmann-json.txt`.
The Linux x86_64 fetch fallback is pinned to the 1.28.2 package published by
`csukuangfj/onnxruntime-libs`, with SHA-256
`c4f8994d56191d9d2c92a961b39fe790459f2c5d155f912b239506ea31359534`.
The ONNX Runtime license text is retained in `LICENSES/onnxruntime.txt`.
Other platforms may provide their own compatible ONNX Runtime installation.

The CLAP backend does not bundle model weights. CI may download the
`ConceptualMachines/magda-sample-tagger` fixtures for runtime validation.
The pinned SHA-256 values are:

- `clap_audio.onnx`:
  `3f42f71e555b62709910b6efa66fa5879f00d9571874b12b0fa674f82dbfe332`
- `clap_text.onnx`:
  `c07b27204836877d5b615c103685b66ea8f21bc6b5b70a572be356125423a8bf`
- `tokenizer.json`:
  `4fd1d86b4f5b53f40a609fcd11c1f34024b735f870a07439d70202b98493661a`

Model artifacts remain subject to the normal provenance/license policy.

Planned permissive backends also include GTSAM (BSD), WORLD (BSD-like), and
selected permissively licensed model artifacts. Code licenses and model-artifact
licenses are audited separately.

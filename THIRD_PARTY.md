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
FetchContent is pinned to commit `bcb845434495e293df3d48f1203b7a86e1852449`;
the upstream license text is retained in `LICENSES/ODAS.txt`. `odas_ros` is not
a dependency.

Planned permissive backends also include sherpa-onnx (Apache-2.0), GTSAM (BSD),
WORLD (BSD-like), WebRTC AEC3 (BSD), and selected permissively licensed model
artifacts. Code licenses and model-artifact licenses are audited separately.

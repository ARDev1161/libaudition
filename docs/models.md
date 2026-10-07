# Model artifacts

Model weights are licensed independently of inference runtimes. No model may be
part of a release until `models/manifest.yaml` records provenance and license.

Required fields for a distributable model:

```yaml
- id: example
  version: "1"
  task: asr
  backend: sherpa_onnx
  source_uri: https://example.invalid/model
  source_revision: immutable-revision
  sha256: hex-digest
  license: Apache-2.0
  commercial_use: true
  redistribution: true
  research_only: false
  providers: [cpu]
```

A CI license check should eventually reject missing/unknown licenses and prevent
research-only models from entering release bundles.

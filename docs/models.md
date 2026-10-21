# Model artifacts and release permissions

Model weights and associated tokenizers have licensing and provenance independent
of their inference runtimes. Publicly downloadable assets are not automatically
approved for commercial use or redistribution.

The checked-in `models/manifest.yaml` currently inventories **unbundled CI
fixtures**, not an approved set of distributable weights. Each fixture records
`id`, `task`, `backend`, `source`, `source_revision`, `sha256`,
`license`, `commercial`, `redistributable`, `bundled` and `purpose`.
Some entries additionally use `license_review: pending`.

`source_revision: UNPINNED-main` explicitly denotes a mutable upstream
reference. A SHA-256 check protects against silently accepting changed content,
but does not make the download reproducible if the remote `main` moves.

For a **future distributable** model, record the verified immutable source
revision, digest, precise model and dataset license provenance, commercial-use
and redistribution permissions, required attribution, and any model-specific
conditions. Only then may `bundled` be considered for a release. Do not infer
permissions from a repository license label without checking the weights and
training-data terms.

The `ci_model_manifest_regressions` and `ci_model_manifest_audit` CTests
check required fixture metadata, URL/revision consistency and that registered
SHA-256 values occur in workflow checksum commands for exact filenames. These are **consistency
checks**, not proof that a URL resolves to that digest, that the upstream
revision is immutable, or that the model is legally redistributable. Actual
downloads are separately checked by real-model CI jobs.

Release gating and remaining gaps are tracked in
`docs/release_v1_acceptance.md`. No model weight is bundled by default.

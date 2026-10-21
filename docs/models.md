# Model artifacts and release permissions

Model weights and associated tokenizers have licensing and provenance independent
of their inference runtimes. Publicly downloadable assets are not automatically
approved for commercial use or redistribution.

The checked-in `models/manifest.yaml` currently inventories **13 unbundled CI
fixtures**, not an approved set of distributable weights. Each fixture records
`id`, `task`, `backend`, `source`, `source_revision`, `sha256`,
`license`, `commercial`, `redistributable`, `bundled` and `purpose`.
Every entry has an explicit `license_review` state. `pending` requires
`commercial: false` and `redistributable: false` even when the
repository-declared license is permissive. An `approved` entry must include
a nonempty, existing repository-local `docs/legal/*.md` `license_evidence`
document; this is a manifest consistency guard, **not** independent legal
verification or automatic permission to redistribute. Silero and YAMNet are
currently marked pending rather than implying completed review.

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

### Additional CI-only model assets (release audit)

GTCRN `gtcrn_simple.onnx` and the Piper Amy Low INT8 `tar.bz2` archive
are registered in `models/manifest.yaml` with the SHA-256 values checked by
the CI download steps. The checks establish downloaded **byte identity**, not
ownership, upstream immutability or redistribution rights. Both are unbundled,
their license review is pending, and neither is approved for commercial
distribution. The Piper archive may contain voice and eSpeak-related assets
with distinct license obligations; inspect its contents before any packaging.
The GitHub release URLs use release tags rather than immutable asset IDs;
their source revisions remain explicitly unverified.

### Whisper tokenizer integrity gap

The Sherpa real-Whisper CI job fetches `tiny.en-tokens.txt` from the same
pinned Hugging Face revision as its encoder and decoder. The ONNX files are
checked against fixed SHA-256 values, but the tokenizer currently has only
a nonempty-file check and is not separately listed in the 13-entry manifest.
This is **not** an integrity guarantee. Before promoting the model fixture
provenance gate, independently determine and verify its SHA-256, register
the tokenizer as its own unbundled, license-pending artifact, and enforce the
exact digest/filename in CI. No tokenizer hash is claimed here.

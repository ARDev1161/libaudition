# Frozen three-way ODAS SSL/SST/SSS comparison (experimental)

Run **three actual offline ODAS replays** on the same immutable Acoustix WAV:
(1) upstream ODAS, (2) pinned `ARDev1161/odas:dev` SHA,
(3) a separate task feature SHA. The feature must not silently change the
source WAV, seed, 4-microphone cross geometry (±0.032 m), PCM map 1/2/3/4,
SSL/SST/SSS parameters or oracle. Use SHA256 of a canonical serialized
configuration including geometry, PCM map, algorithm options and seed;
include all three exact commit SHAs. Hash the *actual* input WAV and oracle
bytes, not paths or labels.

The measured per-run JSON schema is the input contract of
`tests/integration/acoustix_spatial_paired_ab.py`, with extra
`odas_commit_sha` (40 lowercase hex characters) and `odas_repository`.
Every `ssl`, `sst` and `sss` metric must come from measured outputs.
Never fill unknown values with zero. Missing metrics cause exit code 2
(`BLOCKED_MISSING_METRICS`). An observed regression against dev causes
exit code 1; a complete comparison without negative metric deltas exits
zero but is **not** product acceptance. No confidence intervals or
tolerances are inferred.

Example, after producing real `upstream.json`, `dev.json`, `feature.json`:

```sh
python3 tests/integration/acoustix_odas_three_way_ab.py \
  --upstream upstream.json --dev dev.json --feature feature.json \
  --output three_way_ab.json
```

A feature should be evaluated independently on frozen T00/T02/T03/T04/T07,
including per-speaker clean/convolved references, SI-SDRi/SIRi, leakage,
speech retention and WER where a pinned ASR model and human transcripts
are available. Missing SSS is **BLOCKED**, even when SSL detects two
directions or SST emits two tracks. Report both baseline-to-dev and
dev-to-feature; do not retune on held-out cases.

**Current status:** comparator and deterministic contract tests only.
No complete upstream/dev/feature measured SSS triplet is available from
this PR. The real T03 frozen diagnostic is a separate characterization:
243 two-voice overlap hops, 214 SSL pair hits, zero SST pair hits.
It is not an acoustic separation quality score.

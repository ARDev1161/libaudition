# Measured C++ branch coverage audit

This audit is separate from the algorithm CTest registration contract and from
real-recording / hardware-quality validation. None of those three is a substitute
for the others.

## Baseline and scope

The last **successful** measured baseline before this change is CI #1190,
commit e82221d: 2791/2889 C++ lines (96.6%), 2541/3703 branches (68.6%).
It includes only the instrumented core under src/. CI #1191 failed during
compilation, so it did **not** produce a new valid coverage baseline.

The 31 behavioral regression tests in commit e82221d exercised many already
covered paths, raising branch coverage by only one branch. This demonstrates
why a new test count is not a coverage metric.

## What CI now produces

The cpp-coverage job runs gcovr with the same core filter and unreachable-branch
option for both summaries and a detailed coverage.json. It then runs
tools/audit_gcovr_branches.py to generate:

- branch-audit.json: per-file hit/miss counts, source line, uncovered branch
  indices, and an explicit unreviewed marker for every location.
- branch-audit.md: file ranking and a limited shortlist of missing locations.
- coverage.json and coverage.html: raw evidence to inspect an individual path.

These files are uploaded in the existing libaudition-cpp-core-coverage artifact.
The Markdown summary also appears in the GitHub Actions job summary. The
script's fail-closed parsing and sorting are tested by
branch_coverage_audit_selftest in CTest.

The *uncovered* classification only means no instrumented test entered the
branch in this CI configuration. It does not establish that the path is feasible,
unreachable, dead, or compiler-generated.

## Rules for closing a gap

1. Locate the exact source condition and inspect the raw gcovr branch data.
2. Search existing tests: if behavior is already asserted, avoid a duplicate.
3. Classify the gap only with evidence: reachable, error-injection-dependent,
   optional-platform-dependent, compiler-generated, or provably unreachable.
4. For a reachable path, write a test that asserts state, output, error handling,
   or an invariant, not merely successful execution.
5. Run the relevant CTest subset locally, then full CI. Compare covered branch
   counts against the last successful baseline and explain any regressions.
6. Do not weaken production validation, test assertions, or the coverage
   thresholds just to improve a percentage.

High-risk review areas include WAV parsing and streaming I/O, concurrent
per-source classification queues, source identity epochs and eviction, and
transactionality/numerical failure handling in spatial processing. Hardware and
real-model accuracy require separate measured fixtures and explicit quality
thresholds. Microphone-array geometry must not be fixed to ReSpeaker: spatial
processing comes through backends including ODAS.

A 70% branch target is only a planning milestone, not a claim of full algorithm
verification and not a reason to add redundant tests.

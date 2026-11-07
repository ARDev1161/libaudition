# Acoustix S2: real-voice SST probation and SSL hypothesis audit

**Status: diagnostic O2 evidence, not an SST algorithm fix.** This document
uses the *actual measured* ODAS per-hop SSL/SST JSONL from the successful
[paired Acoustix run #42410846601](https://github.com/ARDev1161/libaudition/actions/runs/42410846601)
on the 7×7 m moving-robot room and real licensed LibriSpeech voices.
The report is reproduced automatically with
`tests/integration/acoustix_s2_probation_audit.py` from the archived, unchanged
six-channel PCM. It neither synthesizes tracks nor invents quality metrics.

## Reproduction and provenance

T03 real-microphone WAV: SHA256
`c14fe750e47064de1b105c5fceab1ffe8e5330157b7519818a5c175d6c54f0ee`.
All T02/T03/T07 WAVs: 240000 frames at 16000 Hz, six channels, 1875
recorded 128-frame SST hops and zero recorder drops. Frozen real-speech
RIR artifact is reused, never rerendered between comparisons.

Provisional SST confirmation in the pinned offline runner uses the
unchanged `OdasOptions` defaults: `probability_window=5` hops (40 ms)
and `probability_threshold=0.8`. The offline runner overrides only
SST track/candidate capacity, not these probability settings.
**The raw v1 trace does not embed these settings.** CI explicitly verifies
the two header defaults and absence of overrides, then supplies them
as mandatory arguments. This prevents silently treating unknown
tuning as measured truth.

Reproduce from the root of an extracted run artifact:

    python3 -m unittest discover -s tests/integration \
      -p 'test_acoustix_s2_probation_audit.py' -v
    python3 tests/integration/acoustix_s2_probation_audit.py \
      --root build-s2 \
      --output build-s2/real_speech_sst_probation_audit.json \
      --probation-window 5 --probation-threshold 0.8

The tool cross-checks all birth IDs, ODAS timestamps and 128-frame
offsets, PCM SHA, recorder drops and oracle hop continuity. It
independently reconstructs per-existing-track existence probability
using exactly the ODAS per-hop union:
`P(track) = 1 - product_pot(1 - P(track | candidate))`.
It averages **five subsequent** hops after the birth hop; the birth hop
does not count as an observed probability. It fails if this independently
predicted probation decision contradicts the actual SST promotion/retirement
event, rather than calling a malformed trace PASS.

## Quantitative observations before algorithm changes

Measured from the real T03 raw trace and existing ground truth:

- **243** oracle hops with exactly two voice sources active; **zero**
  hops with two simultaneously active SST tracks.
- Over those 243 hops, there exist two *distinct* SSL candidates,
  one-to-one within 25° of both source directions on **97** hops, or
  within 45° on **214** hops. The median two-voice oracle angular
  separation was **31.48°**; it varies substantially as the robot moves.
- The broad 45° match is geometric coincidence, **not** evidence of
  independent speech separation. Some apparent candidates are elevated,
  weak or unstable, even when their azimuth looks plausible.
- SST made **13** provisional birth decisions across T03; **one** ID
  passed probation, **12** failed. Seven provisional IDs were born
  during the two-voice oracle overlap and none was confirmed.
- Example rejected P-track ID **4**, born on hop 142, received
  consecutive activity values approximately
  `[1.0, 0.998, 0.0002, 0.0, 0.0]`, mean **0.3996**, below **0.8**.
  ID **9** also averaged approximately **0.3994**.
- Control T02 yielded **18 provisional births** but only **one**
  confirmation; T07 yielded **51 provisional births** and **six**
  confirmations, with at most **four** simultaneously active tracks.
  Therefore merely decreasing probation threshold risks additional
  false/short tracks. These counts are not unique human-speaker counts.

The dominant *observed* failure mode for second-voice SST acquisition is
**lack of persistent independent association probability across the
five-hop confirmation window**. This does not yet establish whether
the underlying cause is SSL angular ambiguity, reverberant overlap,
SST track association, beamformer limitations or a mixture. The
already-fixed same-hop pending-slot collision is a separate bug.

## Next falsifiable experiments

1. Keep the same T03 PCM SHA and oracle while comparing SSL azimuth and
   elevation candidate stability and association probability histories,
   stratified by speaker angular separation and actual activity.
2. Use T00 silence and T02 one-voice controls to quantify false track
   lifespan before altering `theta_prob`, `N_prob`, or SST priors.
3. Use T07 and held-out real voices to check source precision, ID handoff,
   fragmentation and missed source hops after any prospective fix.
4. Independently align real source convolved stems and transcripts for
   SI-SDRi/SIRi and ASR WER; **NOT MEASURED** in this PR.
5. Physical ReSpeaker acceptance remains separate.

This is real, reproducible software diagnosis: synthetic unit-test
records only check the analyzer's contract; the CI result is derived
from the frozen actual Acoustix+ODAS WAV replay and saved trace.

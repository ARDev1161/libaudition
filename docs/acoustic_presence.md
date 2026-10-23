# Acoustic presence versus ODAS tracking

The public `audition::dsp::IAcousticPresenceDetector` interface reports acoustic
evidence from PCM. It is deliberately independent of ODAS SST, source identity,
recording and ASR lifecycle.

The initial `RmsAcousticPresenceDetector` is a **diagnostic baseline**, not
a speech detector, VAD, learned noise model or calibrated tracker confidence.
It uses a fixed RMS threshold (default -55 dBFS), reports `calibrated=false`,
and has no hysteresis, warm-up or track-retirement controls.

The abandoned Qt `PerSourceAcousticPresence` from PR #65 calculated its noise
floor from the first source frame. Sustained speech at a nearly constant level
could therefore have near-zero estimated SNR and be retired incorrectly.
That gate is **not** wired into Qt, ODAS, SourceTrackLifecycle or WAV/ASR.

## Investigating persistent SST sources

Instrument the upstream `mod_sst_obj` fields per live slot: `ids`, `type`,
`sourceActivities`, `n_inactive`, and `N_inactive`. Compare those values
against ODAS SSL proposals and separated PCM energy during voice -> silence ->
voice. In libodas, a track can only reach the inactivity retirement test
when its internal conditions and inactivity counters advance; a high
`sourceActivities` value does not by itself prove an audible source exists.

Keep `IAcousticPresenceDetector` output observational. Any future attempt
to couple it to track retirement requires real-array validation, false-retirement
rate measurement, and a separate API/design review.

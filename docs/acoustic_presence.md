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

Keep `IAcousticPresenceDetector` output observational; it is not a
calibrated permission source for native ODAS SST.

## Qt demo: explicit source-silence retirement

The Qt live adapter has a separate **fixed, absolute SSS RMS** timeout.
After 3.0 s below -55 dBFS (both adjustable; 0 s disables), it closes the
source WAV and ASR generation and hides that track even if ODAS still
reports the numeric ID. It does not reset or alter native SST state, and
has no learned noise floor that could mistake continuous speech for silence.
It reads separated/postfiltered PCM **before** optional NS and AGC. A dormant
ID needs a hop at least 6 dB louder than the threshold to start a fresh
recording/ASR generation. Actual ODAS ID disappearance clears dormancy.

This is intentionally **not** the speech VAD state: ordinary pauses between
words are VAD-silent and must not remove a source. The absolute level is not
calibrated for distance or microphone sensitivity. Quiet speech may falsely
retire, while strong competing-source bleed may defeat the timeout. Test on
real ReSpeaker audio, compare disabled/enabled, and track false retire/rearm
counts before considering this policy production-ready.

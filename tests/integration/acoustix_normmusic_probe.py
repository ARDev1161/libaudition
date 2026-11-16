#!/usr/bin/env python3
"""Experimental narrowband NormMUSIC SSL baseline for frozen Acoustix PCM16.

Diagnostic SSL only: not an SST/SSS backend, and no quality PASS is implied.
Uses the four ReSpeaker cross microphones in Acoustix channels 1..4.
"""
import argparse
import json
import wave
import numpy as np

MIC_XY = np.array([[0.032, 0], [0, 0.032], [-0.032, 0], [0, -0.032]])
C = 343.0


def read_acoustix(path):
    with wave.open(str(path), "rb") as wav:
        if wav.getnchannels() != 6 or wav.getsampwidth() != 2:
            raise ValueError("expected frozen Acoustix six-channel PCM16 WAV")
        rate = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    samples = np.frombuffer(raw, dtype="<i2").reshape(-1, 6)
    return rate, samples[:, 1:5].astype(np.float64) / 32768.0


def music_spectrum(samples, rate, nfft=512, hop=128, sources=1):
    """Return angular pseudo-spectrum; fixed one-source hypothesis by default.

    Wideband normalized MUSIC: average normalized frequency-bin spectra.
    Requires enough STFT frames for a stable covariance estimate.
    """
    if samples.ndim != 2 or samples.shape[1] != 4:
        raise ValueError("four microphone channels required")
    if sources < 1 or sources >= 4:
        raise ValueError("sources must be in 1..3")
    if len(samples) < nfft + hop * 7:
        raise ValueError("at least eight STFT frames required")
    frames = np.stack([samples[i:i+nfft] for i in range(0, len(samples)-nfft+1, hop)])
    frames = frames * np.hanning(nfft)[None, :, None]
    stft = np.fft.rfft(frames, axis=1)
    freqs = np.fft.rfftfreq(nfft, 1 / rate)
    angles = np.arange(-180, 180, dtype=np.float64)
    unit = np.stack((np.cos(np.deg2rad(angles)), np.sin(np.deg2rad(angles))), axis=1)
    delays = (unit @ MIC_XY.T) / C
    spectrum = np.zeros(len(angles))
    used = 0
    for bin_index in np.flatnonzero((freqs >= 500) & (freqs <= min(3500, rate / 2))):
        x = stft[:, bin_index, :]
        cov = x.conj().T @ x / len(x)
        if np.trace(cov).real < 1e-12:
            continue
        _, eigvec = np.linalg.eigh(cov)
        noise = eigvec[:, :4-sources]
        steering = np.exp(-2j * np.pi * freqs[bin_index] * delays)
        projection = steering.conj() @ noise
        denominator = np.sum(abs(projection) ** 2, axis=1)
        score = 1 / np.maximum(denominator, 1e-12)
        spectrum += score / max(score.max(), 1e-12)
        used += 1
    if not used:
        raise ValueError("no non-silent frequency bins")
    return angles, spectrum / used, used


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("wav")
    p.add_argument("--sources", type=int, default=1)
    p.add_argument("--output", required=True)
    args = p.parse_args()
    rate, audio = read_acoustix(args.wav)
    angles, spectrum, bins = music_spectrum(audio, rate, sources=args.sources)
    result = {"algorithm": "experimental_normmusic", "status": "diagnostic_only",
              "rate_hz": rate, "mic_geometry_m": MIC_XY.tolist(),
              "pcm_channels_zero_based": [1, 2, 3, 4],
              "source_hypothesis": args.sources, "frequency_bins": bins,
              "peak_azimuth_deg": float(angles[np.argmax(spectrum)]),
              "angles_deg": angles.tolist(), "normalized_spectrum": spectrum.tolist()}
    with open(args.output, "w", encoding="utf-8") as out:
        json.dump(result, out, indent=2)


if __name__ == "__main__":
    main()

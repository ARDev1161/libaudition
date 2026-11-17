#!/usr/bin/env python3
"""Experimental narrowband NormMUSIC SSL baseline for frozen Acoustix PCM16.

Diagnostic SSL only: not an SST/SSS backend, and no quality PASS is implied.
Uses the four ReSpeaker cross microphones in Acoustix channels 1..4.\nGeometry is read in the exact order of table_conversation_7x7.json: -X,-Y,+X,+Y.
"""
import argparse
import json
import wave
import numpy as np

MIC_XY = np.array([[-0.032, 0], [0, -0.032], [0.032, 0], [0, 0.032]])
C = 343.0


def read_acoustix(path):
    with wave.open(str(path), "rb") as wav:
        if wav.getnchannels() != 6 or wav.getsampwidth() != 2:
            raise ValueError("expected frozen Acoustix six-channel PCM16 WAV")
        rate = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    samples = np.frombuffer(raw, dtype="<i2").reshape(-1, 6)
    return rate, samples[:, 1:5].astype(np.float64) / 32768.0


def music_spectrum(samples, rate, nfft=512, hop=128, sources=1, algorithm="auto"):
    """Return angular pseudo-spectrum; fixed one-source hypothesis by default.

    Wideband normalized MUSIC: average normalized frequency-bin spectra.
    Requires enough STFT frames for a stable covariance estimate.
    """
    if samples.ndim != 2 or samples.shape[1] != 4:
        raise ValueError("four microphone channels required")
    if sources < 1 or sources > 4:
        raise ValueError("sources must be in 1..4")
    if algorithm not in ("auto", "normmusic", "srp-phat"):
        raise ValueError("unknown SSL algorithm")
    if algorithm == "normmusic" and sources == 4:
        raise ValueError("four-microphone MUSIC cannot resolve four independent sources")
    method = ("srp-phat" if sources == 4 else "normmusic") if algorithm == "auto" else algorithm
    if not np.isfinite(samples).all() or rate <= 0:
        raise ValueError("finite PCM and positive sample rate required")
    if len(samples) < nfft + hop * 7:
        raise ValueError("at least eight STFT frames required")
    # Covariance uses x x^H; steering phase is exp(-j omega tau).
    frames = np.stack([samples[i:i+nfft] for i in range(0, len(samples)-nfft+1, hop)])
    frames = frames * np.hanning(nfft)[None, :, None]
    stft = np.fft.rfft(frames, axis=1)
    freqs = np.fft.rfftfreq(nfft, 1 / rate)
    angles = np.arange(-180, 180, dtype=np.float64)
    unit = np.stack((np.cos(np.deg2rad(angles)), np.sin(np.deg2rad(angles))), axis=1)
    delays = (unit @ MIC_XY.T) / C
    spectrum = np.zeros(len(angles))
    used = 0
    # Four microphones cannot provide a nonempty MUSIC noise subspace for
    # four independent sources. For this case use explicit pairwise SRP-PHAT
    # rather than silently pretending a four-source MUSIC decomposition exists.
    pairs = [(i, j) for i in range(4) for j in range(i + 1, 4)]
    for bin_index in np.flatnonzero((freqs >= 500) & (freqs <= min(3500, rate / 2))):
        x = stft[:, bin_index, :]
        cov = x.T @ x.conj() / len(x)
        if np.trace(cov).real < 1e-12:
            continue
        if method == "srp-phat":
            score = np.zeros(len(angles))
            for i, j in pairs:
                cross = cov[i, j]
                if abs(cross) > 1e-12:
                    # cov[i,j] = X_i * conj(X_j) has phase -omega*(tau_i-tau_j).
                    # SRP steering must conjugate that phase, not double it.
                    phase = np.exp(+2j * np.pi * freqs[bin_index] * (delays[:, i] - delays[:, j]))
                    score += np.real((cross / abs(cross)) * phase)
            score -= score.min()
        else:
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



def select_peaks(angles, spectrum, count, min_separation_deg=20):
    """Pick independent circular local maxima, not repeated adjacent bins."""
    if count < 1:
        raise ValueError("positive peak count required")
    peaks = []
    for i in range(len(spectrum)):
        if spectrum[i] >= spectrum[(i - 1) % len(spectrum)] and spectrum[i] > spectrum[(i + 1) % len(spectrum)]:
            peaks.append(i)
    peaks.sort(key=lambda i: (-float(spectrum[i]), float(angles[i])))
    selected = []
    for i in peaks:
        if all(abs((float(angles[i]) - float(angles[j]) + 180) % 360 - 180) >= min_separation_deg
               for j in selected):
            selected.append(i)
            if len(selected) == count:
                break
    return [float(angles[i]) for i in selected]


def windowed_music(audio, rate, window_seconds=0.5, step_seconds=0.25,
                   sources=1, algorithm="auto"):
    """Emit time-indexed SSL hypotheses; silence windows yield no detection.

    No oracle is consulted. These are hypotheses, not one-to-one speaker
    matches. A fixed source count is an experimental assumption.
    """
    if not np.isfinite(window_seconds) or not np.isfinite(step_seconds):
        raise ValueError("window and step must be finite")
    if window_seconds <= 0 or step_seconds <= 0:
        raise ValueError("window and step must be positive")
    if audio.ndim != 2 or audio.shape[1] != 4 or rate <= 0:
        raise ValueError("four channels and positive rate required")
    width = int(round(window_seconds * rate))
    step = int(round(step_seconds * rate))
    if width < 512 + 128 * 7 or step < 1:
        raise ValueError("window too short or step below one sample")
    result = []
    for start in range(0, len(audio) - width + 1, step):
        segment = audio[start:start + width]
        try:
            angles, spectrum, bins = music_spectrum(segment, rate,
                                                    sources=sources, algorithm=algorithm)
        except ValueError as exc:
            if str(exc) == "no non-silent frequency bins":
                result.append({"start_sample": start, "end_sample": start + width,
                               "status": "silent", "peak_azimuth_deg": None, "azimuths_deg": []})
                continue
            raise
        result.append({"start_sample": start, "end_sample": start + width,
                       "status": "hypothesis",
                       "peak_azimuth_deg": float(angles[np.argmax(spectrum)]),
                       "azimuths_deg": select_peaks(angles, spectrum, sources),
                       "frequency_bins": bins})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("wav")
    p.add_argument("--sources", type=int, default=1)
    p.add_argument("--algorithm", choices=("auto", "normmusic", "srp-phat"), default="auto")
    p.add_argument("--output", required=True)
    p.add_argument("--window-seconds", type=float, default=0.5)
    p.add_argument("--step-seconds", type=float, default=0.25)
    args = p.parse_args()
    rate, audio = read_acoustix(args.wav)
    windows = windowed_music(audio, rate, args.window_seconds,
                             args.step_seconds, sources=args.sources, algorithm=args.algorithm)
    # A completely silent file is a valid negative fixture (T00), not a crash.
    # Keep the same schema but explicitly emit no global direction.
    try:
        angles, spectrum, bins = music_spectrum(audio, rate, sources=args.sources, algorithm=args.algorithm)
        global_status = "hypothesis"
        global_peak = float(angles[np.argmax(spectrum)])
    except ValueError as exc:
        if str(exc) != "no non-silent frequency bins":
            raise
        angles = np.arange(-180, 180, dtype=np.float64)
        spectrum = np.zeros(len(angles))
        bins = 0
        global_status = "silent"
        global_peak = None
    result = {"algorithm": "experimental_" + (("srp_phat" if args.sources == 4 else "normmusic") if args.algorithm == "auto" else args.algorithm.replace("-", "_")), "status": "diagnostic_only",
              "rate_hz": rate, "mic_geometry_m": MIC_XY.tolist(),
              "pcm_channels_zero_based": [1, 2, 3, 4],
              "source_hypothesis": args.sources, "frequency_bins": bins,
              "peak_azimuth_deg": global_peak,
              "global_status": global_status,
              "azimuths_deg": select_peaks(angles, spectrum, args.sources) if bins else [],
              "angles_deg": angles.tolist(), "normalized_spectrum": spectrum.tolist(),
              "windows": windows}
    with open(args.output, "w", encoding="utf-8") as out:
        json.dump(result, out, indent=2)


if __name__ == "__main__":
    main()

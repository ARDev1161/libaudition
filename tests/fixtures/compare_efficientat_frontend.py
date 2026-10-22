#!/usr/bin/env python3
"""Compare EfficientAT C++ frontend against upstream PyTorch + torchaudio.

Usage:
  python3 tests/fixtures/compare_efficientat_frontend.py build-eat/efficientat_mel.f32

Requires torch and torchaudio. No network or model weights required.
"""
import math
import sys
from pathlib import Path

import numpy as np
import torch
import torchaudio

path = Path(sys.argv[1])
actual = np.fromfile(path, dtype="<f4").reshape(128, 100)
n = np.arange(32000, dtype=np.float64)
t = n / 32000.0
signal = (
    0.1 * np.sin(2 * math.pi * 440.0 * t)
    + 0.05 * np.cos(2 * math.pi * 1730.0 * t)
    + 0.02 * np.sin(2 * math.pi * (250.0 * t + 300.0 * t * t))
).astype(np.float32)

with torch.no_grad():
    x = torch.from_numpy(signal).reshape(1, -1)
    preemphasis = torch.tensor([[[-0.97, 1.0]]], dtype=torch.float32)
    x = torch.nn.functional.conv1d(x.unsqueeze(1), preemphasis).squeeze(1)
    window = torch.hann_window(800, periodic=False)
    stft = torch.stft(x, n_fft=1024, hop_length=320, win_length=800,
                      center=True, normalized=False, window=window,
                      return_complex=True)
    power = stft.abs().square()
    banks, _ = torchaudio.compliance.kaldi.get_mel_banks(
        128, 1024, 32000, 0.0, 15000.0,
        vtln_low=100.0, vtln_high=-500.0, vtln_warp_factor=1.0)
    banks = torch.nn.functional.pad(banks, (0, 1))
    expected = ((torch.log(torch.matmul(banks, power) + 1e-5) + 4.5) / 5.0)
expected = expected.squeeze(0).cpu().numpy()

diff = np.abs(expected - actual)
print(f"tensor shape={actual.shape}, max_abs={diff.max():.9g}, "
      f"mean_abs={diff.mean():.9g}, p99={np.percentile(diff,99):.9g}")
# Precision differs between C++ double-FFT accumulation and the PyTorch float
# implementation. A 3e-3 absolute envelope is intentionally tight enough
# to catch window, padding, mel-bank and normalization mistakes.
if not np.isfinite(diff).all() or diff.max() > 3e-3:
    raise SystemExit("FAIL: EfficientAT mel frontend differs from PyTorch reference")
print("PASS: EfficientAT mel frontend numerical parity")

"""Feature front-end: Hann window, FFT magnitude, projection onto a 20-cent
grid, log and normalization. swiftf0_frontend.c must match it.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import numpy as np

SR          = 16000
NFFT        = 1024
NSPEC       = NFFT // 2 + 1
NBINS_IN    = 480
NBINS_LIVE  = 476
NBINS_OUT   = 360
FMIN        = 32.70
CENTS_PER_BIN = 20.0
LOG_EPS     = 1e-6
LOG_FLOOR   = -16.0

def grid_hz(nbins: int = NBINS_OUT) -> np.ndarray:
    return FMIN * np.exp2(np.arange(nbins) / 60.0)

def hann_periodic(n: int = NFFT) -> np.ndarray:
    return 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / n)

def projection_matrix() -> np.ndarray:
    W = np.zeros((NBINS_IN, NSPEC), dtype=np.float64)
    fft_hz = np.arange(NSPEC) * (SR / NFFT)
    centers = grid_hz(NBINS_LIVE)
    for k in range(NBINS_LIVE):
        lo = centers[k - 1] if k > 0 else centers[0] / 2 ** (1 / 60)
        hi = centers[k + 1] if k + 1 < NBINS_LIVE else centers[k] * 2 ** (1 / 60)
        half_lo = max(centers[k] - lo, SR / NFFT)
        half_hi = max(hi - centers[k], SR / NFFT)
        w = np.where(
            fft_hz <= centers[k],
            1.0 - (centers[k] - fft_hz) / half_lo,
            1.0 - (fft_hz - centers[k]) / half_hi,
        )
        w = np.clip(w, 0.0, None)
        s = w.sum()
        if s > 0:
            W[k] = w / s
    return W.astype(np.float32)

_W = None
_HANN = None

def features(wave_i16: np.ndarray) -> np.ndarray:
    global _W, _HANN
    if _W is None:
        _W, _HANN = projection_matrix(), hann_periodic().astype(np.float32)
    x = wave_i16.astype(np.float32) / 32768.0
    spec = np.abs(np.fft.rfft(x * _HANN, axis=-1)).astype(np.float32)
    proj = spec @ _W.T
    lg = np.log2(proj + LOG_EPS)
    lg = lg - lg.max(axis=-1, keepdims=True)
    lg = np.maximum(lg, LOG_FLOOR)
    return (lg / 8.0 + 1.0).astype(np.float32)

def f0_to_bin(f0_hz):
    return 60.0 * np.log2(np.asarray(f0_hz, dtype=np.float64) / FMIN)

def bin_to_f0(b):
    return FMIN * np.exp2(np.asarray(b, dtype=np.float64) / 60.0)

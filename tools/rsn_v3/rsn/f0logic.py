"""Target pitch contours for training, following the firmware's F0 logic.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import numpy as np

HOP_S = 0.005
EMA_ALPHA = 0.3
ONSET_CENTS = 80.0
ONSET_HOLD_HOPS = 3
VIB_HIST = 64
VIB_EVERY = 10
VIB_NFFT = 256
VIB_BINS = (5, 10)
VIB_CENTS = 20.0

MAJOR = (0, 2, 4, 5, 7, 9, 11)


def cents(f, ref):
    return 1200.0 * np.log2(np.maximum(f, 1e-9) / ref)


def snap_semitone(f0: float, scale=None, root: int = 0, ref: float = 440.0):
    if f0 <= 0:
        return f0
    n = 12.0 * np.log2(f0 / ref)
    if scale is None:
        return ref * 2.0 ** (round(n) / 12.0)
    best, bd = None, 1e9
    base = int(np.floor(n / 12.0)) * 12
    for oct_ in (base - 12, base, base + 12):
        for d in scale:
            c = oct_ + (root + d) % 12 + (12 if (root + d) >= 12 else 0)
            if abs(c - n) < bd:
                best, bd = c, abs(c - n)
    return ref * 2.0 ** (best / 12.0)


def vibrato_flag(hist_hz: np.ndarray) -> bool:
    h = np.asarray(hist_hz, dtype=np.float64)
    h = h[h > 0]
    if len(h) < VIB_HIST:
        return False
    c = cents(h, np.exp(np.mean(np.log(h))))
    w = np.hanning(len(c))
    X = np.abs(np.fft.rfft(c * w, VIB_NFFT))
    amp = 2.0 * X / max(w.sum(), 1e-9)
    lo, hi = VIB_BINS
    return bool(amp[lo:hi + 1].max() > VIB_CENTS)


def f0_corr_contour(f0_auth: np.ndarray, voiced: np.ndarray, shift_st: float = 0.0,
                    mode: str = "snap", vib: str = "ratio", scale=None,
                    root: int = 0, ref: float = 440.0) -> np.ndarray:
    f = np.asarray(f0_auth, dtype=np.float64)
    v = np.asarray(voiced, bool)
    nh = len(f)
    tr = 2.0 ** (shift_st / 12.0)
    if mode == "track":
        return f * tr
    out = np.zeros(nh)
    sm = 0.0
    prev = 0.0
    hold = 0
    hist = []
    vib_on = False
    last_corr = 0.0
    for h in range(nh):
        fa = f[h]
        if fa <= 0:
            out[h] = last_corr
            continue
        onset = (h == 0) or (not v[h - 1] and v[h]) or (
            prev > 0 and abs(cents(fa, prev)) > ONSET_CENTS)
        if onset:
            sm = fa
            hist = []
            hold = ONSET_HOLD_HOPS
            vib_on = False
        else:
            sm += EMA_ALPHA * (fa - sm)
        hist.append(fa)
        if len(hist) > VIB_HIST:
            hist = hist[-VIB_HIST:]
        if h % VIB_EVERY == 0 and len(hist) >= VIB_HIST:
            vib_on = vibrato_flag(np.array(hist))
        if hold > 0:
            corr = snap_semitone(fa, scale, root, ref)
            hold -= 1
        elif vib_on:
            m = float(np.exp(np.mean(np.log(np.array(hist)))))
            s = snap_semitone(m, scale, root, ref)
            corr = fa * (s / m) if vib == "ratio" else s
        else:
            corr = snap_semitone(sm, scale, root, ref)
        last_corr = corr * tr
        out[h] = last_corr
        prev = fa
    first = next((x for x in out if x > 0), 0.0)
    out[out <= 0] = first
    return out


def random_config(rng: np.random.Generator, shift_range=(-4.0, 4.0)):
    r = rng.random()
    if r < 0.15:
        return (0.0, "track", "ratio", None, 0)
    if r < 0.30:
        lo, hi = shift_range
        return (float(rng.uniform(lo, hi)), "track", "ratio", None, 0)
    lo, hi = (shift_range if rng.random() < 0.5 else (-1.0, 1.0))
    s = float(rng.integers(int(np.floor(lo)), int(np.ceil(hi)) + 1))
    vib = "ratio" if rng.random() < 0.7 else "hold"
    if rng.random() < 0.4:
        return (s, "snap", vib, MAJOR, int(rng.integers(0, 12)))
    return (s, "snap", vib, None, 0)

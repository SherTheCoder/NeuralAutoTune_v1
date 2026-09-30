"""Glottal closure detection and LP-PSOLA on the residual, used to build the
training targets.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import numpy as np

from . import dsp

HOP, FS = dsp.HOP, dsp.FS16


def residual_polarity(e: np.ndarray, keep=None) -> float:
    e = np.asarray(e, dtype=np.float64)
    if keep is not None:
        k = np.repeat(np.asarray(keep, bool), HOP)[: len(e)]
        if k.any():
            e = e[k]
    return -1.0 if np.percentile(-e, 99.9) > np.percentile(e, 99.9) else 1.0


def _track(ep: np.ndarray, period_of, s0: int, s1: int, search: float = 0.35):
    seg = ep[s0:s1]
    n = len(seg)
    if n < 4 * HOP:
        return np.array([], dtype=int)
    seed = int(np.argmax(seg))
    out = [seed]
    for direction in (+1, -1):
        t = seed
        while True:
            P = period_of(s0 + t)
            lo = int(round(t + direction * P * (1 - search)))
            hi = int(round(t + direction * P * (1 + search)))
            lo, hi = (lo, hi) if direction > 0 else (hi, lo)
            lo, hi = max(0, lo), min(n, hi + 1)
            if hi - lo < 2:
                break
            nxt = lo + int(np.argmax(seg[lo:hi]))
            if nxt == t:
                break
            out.append(nxt)
            t = nxt
    return np.array(sorted(set(out)), dtype=int) + s0


def detect_gci(e: np.ndarray, f0_per_hop: np.ndarray, voiced: np.ndarray,
               polarity: float | None = None, min_run_hops: int = 8,
               max_dev: float = 0.3):
    e = np.asarray(e, dtype=np.float64)
    f0 = np.asarray(f0_per_hop, dtype=np.float64)
    v = np.asarray(voiced, bool).copy()
    nh = min(len(f0), len(v), len(e) // HOP)
    v = v[:nh]
    pol = residual_polarity(e, v) if polarity is None else float(polarity)
    ep = np.maximum(pol * e, 0.0)

    def period_of(t):
        h = min(int(t) // HOP, nh - 1)
        f = f0[h] if np.isfinite(f0[h]) and f0[h] > 0 else 200.0
        return FS / f

    valid = v.copy()
    gcis = []
    i = 0
    while i < nh:
        if not v[i]:
            i += 1
            continue
        j = i
        while j + 1 < nh and v[j + 1]:
            j += 1
        h0, h1 = i, j + 1
        i = j + 1
        if h1 - h0 < min_run_hops:
            valid[h0:h1] = False
            continue
        inst = _track(ep, period_of, h0 * HOP, min(h1 * HOP, len(e)))
        if len(inst) < 4:
            valid[h0:h1] = False
            continue
        for a_, b_ in zip(inst[:-1], inst[1:]):
            P = 0.5 * (period_of(a_) + period_of(b_))
            dev = abs(np.log((b_ - a_) / P))
            if dev > np.log(1.0 + max_dev):
                valid[a_ // HOP: b_ // HOP + 1] = False
        valid[h0: inst[0] // HOP] = False
        valid[inst[-1] // HOP + 1: h1] = False
        gcis.append(inst)
    gci = np.concatenate(gcis) if gcis else np.array([], dtype=int)
    return gci.astype(np.int64), valid


def _grain_bounds(gci: np.ndarray, m: int, period_of):
    g = int(gci[m])
    P = period_of(g)
    left = right = int(round(P))
    if m > 0 and 0 < g - gci[m - 1] <= 1.6 * P:
        left = int(g - gci[m - 1])
    if m + 1 < len(gci) and 0 < gci[m + 1] - g <= 1.6 * P:
        right = int(gci[m + 1] - g)
    return max(left, 2), max(right, 2)


def psola_target(e_src: np.ndarray, gci: np.ndarray, wraps: np.ndarray,
                 f0_src_hop: np.ndarray, f0_corr_hop: np.ndarray,
                 valid_hop: np.ndarray, lookahead: int = 0,
                 gain: str = "energy", max_reach_periods: float = 1.5,
                 fill: str = "psola"):
    assert fill in ("psola", "source"), fill
    e = np.asarray(e_src, dtype=np.float64)
    n = len(e)
    nh = min(len(f0_src_hop), len(f0_corr_hop), n // HOP)
    valid = np.asarray(valid_hop, bool)[:nh].copy()
    gci = np.asarray(gci, dtype=np.int64)
    wraps = np.asarray(wraps, dtype=np.int64)
    out = np.zeros(n)
    placed = np.zeros(n, bool)
    f0s = np.asarray(f0_src_hop, dtype=np.float64)[:nh]

    def period_of(t):
        h = min(max(int(t), 0) // HOP, nh - 1)
        f = f0s[h] if np.isfinite(f0s[h]) and f0s[h] > 0 else 200.0
        return FS / f

    if len(gci) >= 2:
        for w in wraps:
            hw = int(w) // HOP
            if hw >= nh:
                continue
            hi = int(np.searchsorted(gci, w + lookahead, side="right"))
            if hi == 0:
                valid[hw] = False
                if fill != "psola":
                    continue
                hi = 1
            cand = gci[max(0, hi - 3): hi]
            m = int(max(0, hi - 3) + np.argmin(np.abs(cand - w)))
            g = int(gci[m])
            P = period_of(g)
            if abs(g - w) > max_reach_periods * P:
                valid[hw] = False
                if fill != "psola":
                    continue
            left, right = _grain_bounds(gci, m, period_of)
            win = np.concatenate([0.5 - 0.5 * np.cos(np.pi * np.arange(left) / left),
                                  0.5 + 0.5 * np.cos(np.pi * np.arange(right) / right)])
            src0, src1 = g - left, g + right
            dst0, dst1 = w - left, w + right
            lo = max(0, -src0, -dst0)
            hi_ = min(src1 - src0, n - src0, n - dst0)
            if hi_ <= lo:
                continue
            out[dst0 + lo: dst0 + hi_] += win[lo:hi_] * e[src0 + lo: src0 + hi_]
            placed[dst0 + lo: dst0 + hi_] = True

    f0c = np.asarray(f0_corr_hop, dtype=np.float64)[:nh]
    ratio = np.where((f0s > 0) & (f0c > 0), f0s / np.maximum(f0c, 1e-9), 1.0)
    if gain == "energy":
        g_h = np.sqrt(ratio)
    elif gain == "envelope":
        g_h = ratio
    else:
        g_h = np.ones(nh)
    centers = np.arange(nh) * HOP + HOP / 2.0
    g_s = np.interp(np.arange(n), centers, g_h)
    out *= g_s

    if fill == "psola":
        keep_src = ~placed
    else:
        vmask = np.repeat(valid, HOP)
        keep_src = ~np.pad(vmask, (0, max(0, n - len(vmask))))[:n]
    out = np.where(keep_src, e, out)
    return out, valid


def identity_check(e_src, gci, f0_hop, valid, lookahead=0, offset=0):
    wraps = gci + int(offset)
    tgt, v = psola_target(e_src, gci, wraps, f0_hop, f0_hop, valid,
                          lookahead=lookahead, gain="verbatim")
    ref = np.roll(e_src, int(offset))
    m = np.repeat(v, HOP)[: len(e_src)]
    if len(gci) > 4:
        m[: gci[1] + 1] = False
        m[gci[-2]:] = False
    err = np.sum((tgt - ref)[m] ** 2) / max(np.sum(ref[m] ** 2), 1e-30)
    return float(err)

"""Signal processing shared with the firmware: pre-emphasis, LPC per hop,
residual, excitation, and a differentiable LPC synthesis for the loss.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import os
import sys

import numpy as np
import torch
import torch.nn.functional as F

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "c1"))
sys.path.insert(0, os.path.join(_HERE, "..", "excitation"))

from c1_crossval import H_DEC, levinson          # noqa: E402  board-validated
from excitation_twin import Excitation           # noqa: E402  firmware twin

FS16 = 16000
FS48 = 48000
HOP = 80
WIN = 320
ORDER = 18
PRE = 0.97
UP = 3

RES_SCALE = 20.0    # default only; prep.py measures it (19.6114 for the deployed net)

UP_COEFFS = np.array([
    -1.34456836e-02, -1.53413228e-02, -4.25993679e-03, +1.81605415e-02,
    +3.05260105e-02, +1.21228916e-02, -2.96230627e-02, -5.44453101e-02,
    -2.37437327e-02, +5.11938028e-02, +1.00141545e-01, +5.09694519e-02,
    -8.67666701e-02, -1.93835257e-01, -1.16785788e-01, +1.98860482e-01,
    +6.31206672e-01, +9.42088422e-01, +9.42088422e-01, +6.31206672e-01,
    +1.98860482e-01, -1.16785788e-01, -1.93835257e-01, -8.67666701e-02,
    +5.09694519e-02, +1.00141545e-01, +5.11938028e-02, -2.37437327e-02,
    -5.44453101e-02, -2.96230627e-02, +1.21228916e-02, +3.05260105e-02,
    +1.81605415e-02, -4.25993679e-03, -1.53413228e-02, -1.34456836e-02,
], dtype=np.float64)

_HANN = np.hanning(WIN)


def preemphasis(x16: np.ndarray, state: float = 0.0) -> np.ndarray:
    xp = np.empty_like(x16, dtype=np.float64)
    xp[0] = x16[0] - PRE * state
    xp[1:] = x16[1:] - PRE * x16[:-1]
    return xp


def lpc_per_hop(x16: np.ndarray):
    xp = preemphasis(x16)
    n_hops = (len(xp) - WIN) // HOP + 1
    a = np.zeros((n_hops, ORDER + 1))
    k = np.zeros((n_hops, ORDER))
    for h in range(n_hops):
        seg = xp[h * HOP: h * HOP + WIN] * _HANN
        R = np.array([np.dot(seg[: WIN - i], seg[i:]) for i in range(ORDER + 1)])
        if R[0] < 1e-12:
            a[h, 0] = 1.0
            continue
        R = R.copy()
        R[0] *= 1.0001
        ah, kh = levinson(R, ORDER)
        a[h] = np.concatenate(([1.0], ah))
        k[h] = kh
    return a, k, n_hops


def residual_from_lpc(x16: np.ndarray, a: np.ndarray) -> np.ndarray:
    from scipy.signal import lfilter
    xp = preemphasis(x16)
    n_hops = a.shape[0]
    e = np.zeros(n_hops * HOP)
    for h in range(n_hops):
        base = h * HOP + WIN - HOP
        blk = xp[base - ORDER: base + HOP]
        taps = np.concatenate(([1.0], -a[h, 1:]))
        e[h * HOP: (h + 1) * HOP] = lfilter(taps, [1.0], blk)[ORDER:]
    return e


def residual_sanity(x16: np.ndarray, e: np.ndarray) -> float:
    xp = preemphasis(x16)[WIN - HOP: WIN - HOP + len(e)]
    return float(10.0 * np.log10(np.var(xp) / max(np.var(e), 1e-30)))


def excitation_from_f0(f0_per_hop: np.ndarray, phase0: float = 0.0,
                       voiced=None, run_phases=None) -> np.ndarray:
    osc = Excitation(norm="analytic", f32=True)
    osc.reset(float(f0_per_hop[0]))
    osc.phase = np.float32(phase0)
    if voiced is None:
        return np.concatenate([osc.hop(float(f0)) for f0 in f0_per_hop])
    v = np.asarray(voiced, bool)
    if len(v) < len(f0_per_hop):
        v = np.pad(v, (0, len(f0_per_hop) - len(v)))
    out, run = [], -1
    for h, f0 in enumerate(f0_per_hop):
        if v[h] and (h == 0 or not v[h - 1]):
            run += 1
            osc.reset(float(f0))
            if run_phases is not None and run < len(run_phases):
                osc.phase = np.float32(run_phases[run])
        out.append(osc.hop(float(f0)))
    return np.concatenate(out)


def f0_norm(f0_hz: np.ndarray) -> np.ndarray:
    f0 = np.asarray(f0_hz, dtype=np.float64)
    out = np.zeros_like(f0)
    m = f0 > 0
    out[m] = np.log2(f0[m] / 440.0) / 3.0
    return out


def decimate48(x48: np.ndarray) -> np.ndarray:
    from scipy.signal import lfilter
    return lfilter(H_DEC, 1.0, x48)[::UP]


GAMMA_BW = 0.99
H_MAX = 200.0


class LpcSynth(torch.nn.Module):

    def __init__(self, nfft: int = 1024, gamma: float = GAMMA_BW,
                 h_max: float = H_MAX, upsample: bool = True):
        super().__init__()
        self.upsample = bool(upsample)
        self.gamma = float(gamma)
        self.h_max = float(h_max)
        assert nfft >= 2 * WIN, "nfft must leave room for the filter tail"
        self.nfft = nfft
        up = torch.tensor(UP_COEFFS, dtype=torch.float32).flip(0)
        self.register_buffer("up_k", up.view(1, 1, -1))
        self.register_buffer("hann", torch.tensor(np.hanning(WIN),
                                                  dtype=torch.float32))

    @staticmethod
    def envelope(a: torch.Tensor, nfft: int, gamma: float = GAMMA_BW,
                 h_max: float = H_MAX) -> torch.Tensor:
        if gamma != 1.0:
            g = torch.pow(torch.full_like(a[..., :1], gamma),
                          torch.arange(a.shape[-1], device=a.device,
                                       dtype=a.dtype))
            a = a * g
        taps = torch.cat([a[..., :1], -a[..., 1:]], dim=-1)
        A = torch.fft.rfft(taps, n=nfft, dim=-1)
        H = torch.conj(A) / (A.real ** 2 + A.imag ** 2).clamp_min(1e-30)
        if h_max and np.isfinite(h_max):
            mag = torch.abs(H).clamp_min(1e-30)
            H = H * torch.clamp(h_max / mag, max=1.0)
        return H

    def forward(self, e: torch.Tensor, a: torch.Tensor) -> torch.Tensor:
        B, N = e.shape
        H = a.shape[1]
        assert N == H * HOP, (N, H)

        frames = e.view(B, H, HOP)
        Hf = self.envelope(a, self.nfft, self.gamma, self.h_max)
        Y = torch.fft.rfft(frames, n=self.nfft, dim=-1) * Hf
        y = torch.fft.irfft(Y, n=self.nfft, dim=-1)

        out = F.fold(
            y.transpose(1, 2),
            output_size=(1, (H - 1) * HOP + self.nfft),
            kernel_size=(1, self.nfft), stride=(1, HOP),
        ).view(B, -1)[:, :N]

        out = _deemph(out, PRE)
        if not self.upsample:
            return out

        up = torch.zeros(B, N * UP, device=out.device, dtype=out.dtype)
        up[:, ::UP] = out
        up = F.conv1d(up.unsqueeze(1), self.up_k, padding=UP_COEFFS.size - 1)
        return up.squeeze(1)[:, : N * UP]


def _deemph(x: torch.Tensor, coef: float) -> torch.Tensor:
    y = x
    n = x.shape[-1]
    c = coef
    step = 1
    while step < n:
        shifted = F.pad(y, (step, 0))[..., :n]
        y = y + c * shifted
        c = c * c
        step *= 2
    return y


def synth_exact(e: np.ndarray, a: np.ndarray) -> np.ndarray:
    a = np.asarray(a, dtype=np.float64)
    e = np.asarray(e, dtype=np.float64).reshape(-1)
    nh = a.shape[0]
    assert e.size == nh * HOP, (e.size, nh * HOP)
    y = np.zeros(nh * HOP)
    hist = np.zeros(ORDER)
    for h in range(nh):
        ah = a[h, 1:]
        for n in range(HOP):
            v = e[h * HOP + n] + float(ah @ hist)
            y[h * HOP + n] = v
            hist[1:] = hist[:-1]
            hist[0] = v
    from scipy.signal import lfilter
    y = lfilter([1.0], [1.0, -PRE], y)
    up = np.zeros(y.size * UP)
    up[::UP] = y
    return np.convolve(up, UP_COEFFS)[: y.size * UP]


def excitation_with_wraps(f0_per_hop: np.ndarray, voiced=None, run_phases=None,
                          phase0: float = 0.0):
    from excitation_twin import poly_blep, FS as _FS
    T = np.float32
    f0_per_hop = np.asarray(f0_per_hop, dtype=np.float64)
    nh = len(f0_per_hop)
    v = None if voiced is None else np.asarray(voiced, bool)
    if v is not None and len(v) < nh:
        v = np.pad(v, (0, nh - len(v)))
    out = np.empty(nh * HOP, dtype=T)
    wraps = []
    phase = T(phase0)
    f0_prev = T(f0_per_hop[0])
    run = -1
    inv_fs = T(1.0 / _FS)
    for h in range(nh):
        f0_new = T(f0_per_hop[h])
        if v is not None and v[h] and (h == 0 or not v[h - 1]):
            run += 1
            phase = T(0.0)
            f0_prev = f0_new
            if run_phases is not None and run < len(run_phases):
                phase = T(run_phases[run])
        if f0_prev <= 0.0:
            f0_prev = f0_new
        f0_0 = f0_prev
        df = T((f0_new - f0_0) / T(HOP))
        base = h * HOP
        for n in range(HOP):
            f = T(f0_0 + df * T(n + 1))
            dt = T(f * inv_fs)
            phase = T(phase + dt)
            fl = np.floor(phase)
            if fl >= 1.0:
                wraps.append(base + n)
            phase = T(phase - fl)
            out[base + n] = T(T(T(2.0) * phase - T(1.0)) - T(poly_blep(float(phase), float(dt))))
        f0_prev = f0_new
    out = (out * T(1.7320508)).astype(T)
    return out, np.asarray(wraps, dtype=np.int64)


KCLIP = 0.99


def k_from_a(a: np.ndarray, return_ok: bool = False):
    a = np.asarray(a, dtype=np.float64)
    p = a.shape[-1] - 1
    cur = a[..., 1:].copy()
    k = np.zeros(a.shape[:-1] + (p,))
    ok = np.isfinite(a).all(-1)
    with np.errstate(over="ignore", invalid="ignore", divide="ignore"):
        for i in range(p - 1, -1, -1):
            ki = cur[..., i]
            ok &= np.isfinite(ki) & (np.abs(ki) < 1.0)
            ki = np.clip(np.nan_to_num(ki, nan=0.0, posinf=0.0, neginf=0.0),
                         -KCLIP, KCLIP)
            k[..., i] = ki
            if i == 0:
                break
            den = (1.0 - ki * ki)[..., None]
            prev = (cur[..., :i] + ki[..., None] * cur[..., i - 1::-1][..., :i]) / den
            prev = np.nan_to_num(prev, nan=0.0, posinf=0.0, neginf=0.0)
            cur = np.concatenate([prev, np.zeros_like(cur[..., :p - i])], axis=-1)
    return (k, ok) if return_ok else k


def a_from_k_torch(k: torch.Tensor) -> torch.Tensor:
    p = k.shape[-1]
    a = torch.zeros_like(k)
    for i in range(p):
        ki = k[..., i:i + 1]
        head = a[..., :i] - ki * torch.flip(a[..., :i], dims=[-1])
        a = torch.cat([head, ki, torch.zeros_like(a[..., i + 1:])], dim=-1)
    return torch.cat([torch.ones_like(a[..., :1]), a], dim=-1)


def _allpole_ir(taps: torch.Tensor, n: int) -> torch.Tensor:
    p = taps.shape[-1]
    ir = [torch.ones_like(taps[..., 0])]
    for t in range(1, n):
        m = min(t, p)
        hist = torch.stack(ir[t - m:t][::-1], dim=-1)
        ir.append((taps[..., :m] * hist).sum(-1))
    return torch.stack(ir, dim=-1)


class LpcSynthExact(torch.nn.Module):

    _fallback_hops = 0
    _total_hops = 0
    debug = False
    _reported = set()

    @staticmethod
    def _probe(name, t):
        if name in LpcSynthExact._reported:
            return
        bad = ~torch.isfinite(t)
        if bool(bad.any()):
            LpcSynthExact._reported.add(name)
            fin = t[~bad]
            print(f"    [synth] FIRST non-finite at '{name}': "
                  f"{int(bad.sum())}/{t.numel()} entries, shape {tuple(t.shape)}, "
                  f"finite |max| "
                  f"{float(fin.abs().max()) if fin.numel() else float('nan'):.4g}")

    @classmethod
    def fallback_rate(cls):
        return cls._fallback_hops / max(cls._total_hops, 1)

    def __init__(self, upsample: bool = True, interp: str = "firmware",
                 subframes: int = 4, scan_chunk: int = 32,
                 fp64: bool = False):
        super().__init__()
        self.scan_chunk = int(scan_chunk)
        self.fp64 = bool(fp64)
        assert interp in ("firmware", "hop"), interp
        self.upsample = bool(upsample)
        self.interp = interp
        self.sub = subframes if interp == "firmware" else 1
        assert HOP % self.sub == 0
        self.blk = HOP // self.sub
        assert self.blk >= ORDER, "block must be at least ORDER samples"
        up = torch.tensor(UP_COEFFS, dtype=torch.float32).flip(0)
        self.register_buffer("up_k", up.view(1, 1, -1))

    def _scan(self, Tm, c):
        B, M, p, _ = Tm.shape
        C = max(2, min(int(self.scan_chunk), M))
        eye = torch.eye(p, device=Tm.device, dtype=Tm.dtype)
        outs = []
        s_in = torch.zeros(B, p, 1, device=Tm.device, dtype=Tm.dtype)
        for a0 in range(0, M, C):
            T = Tm[:, a0:a0 + C]
            S = c[:, a0:a0 + C].unsqueeze(-1)
            P = T
            L = T.shape[1]
            d = 1
            while d < L:
                S = S + P @ torch.cat([torch.zeros_like(S[:, :d]), S[:, :-d]], 1)
                P = P @ torch.cat([eye.expand(B, d, p, p), P[:, :-d]], 1)
                d *= 2
            out = S + P @ s_in.unsqueeze(1)
            if LpcSynthExact.debug:
                LpcSynthExact._probe(f"chunk scan @{a0}", out)
            outs.append(out)
            s_in = out[:, -1]
        return torch.cat(outs, 1)

    def block_taps(self, a: torch.Tensor) -> torch.Tensor:
        if self.interp == "hop":
            return a[..., 1:]
        k_np, ok_np = k_from_a(a.detach().float().cpu().numpy().astype(np.float64),
                               return_ok=True)
        k = torch.from_numpy(k_np).to(a.device, a.dtype)
        ok = torch.from_numpy(ok_np).to(a.device)
        LpcSynthExact._fallback_hops += int((~ok_np).sum())
        LpcSynthExact._total_hops += int(ok_np.size)
        k_prev = torch.cat([k[:, :1], k[:, :-1]], dim=1)
        w = torch.arange(1, self.sub + 1, device=a.device, dtype=a.dtype) / self.sub
        ks = k_prev[:, :, None, :] + w[None, None, :, None] * (k - k_prev)[:, :, None, :]
        ks = ks.reshape(a.shape[0], -1, k.shape[-1])
        taps = a_from_k_torch(ks)[..., 1:]
        hop_taps = a[..., 1:].repeat_interleave(self.sub, dim=1)
        return torch.where(ok.repeat_interleave(self.sub, dim=1)[..., None],
                           taps, hop_taps)

    def forward(self, e: torch.Tensor, a: torch.Tensor) -> torch.Tensor:
        if self.fp64 and e.dtype != torch.float64:
            return self._forward(e.double(), a.double()).to(e.dtype)
        return self._forward(e, a)

    def _forward(self, e: torch.Tensor, a: torch.Tensor) -> torch.Tensor:
        B, N = e.shape
        H = a.shape[1]
        assert N == H * HOP, (N, H)
        Lb, p = self.blk, ORDER
        taps = self.block_taps(a)
        M = taps.shape[1]
        ir = _allpole_ir(taps, Lb)
        x = e.reshape(B, M, Lb)
        dbg = LpcSynthExact.debug
        if dbg:
            LpcSynthExact._probe("taps", taps); LpcSynthExact._probe("ir", ir)

        nfft = 1
        while nfft < 2 * Lb - 1:
            nfft *= 2
        IR = torch.fft.rfft(ir, n=nfft, dim=-1)
        y0 = torch.fft.irfft(torch.fft.rfft(x, n=nfft, dim=-1) * IR, n=nfft, dim=-1)[..., :Lb]
        c = torch.flip(y0[..., Lb - p:], dims=[-1])

        idx_n = torch.arange(p, device=e.device)
        j = idx_n[None, :] + idx_n[:, None] + 1
        valid = j <= p
        jj = torch.clamp(j, max=p) - 1
        V = torch.where(valid[None, None], taps[..., jj], taps.new_zeros(()))
        r = idx_n[:, None]; i = idx_n[None, :]
        t = Lb - 1 - r - i
        tv = t >= 0
        G = torch.where(tv[None, None], ir[..., torch.clamp(t, min=0)],
                        ir.new_zeros(()))
        Tm = G @ V
        if dbg:
            LpcSynthExact._probe("y0", y0); LpcSynthExact._probe("c", c)
            LpcSynthExact._probe("V", V); LpcSynthExact._probe("G", G)
            LpcSynthExact._probe("Tm", Tm)

        s = self._scan(Tm, c)
        s_prev = torch.cat([torch.zeros_like(s[:, :1]), s[:, :-1]], dim=1)
        v = (V @ s_prev).squeeze(-1)
        vin = torch.cat([v, torch.zeros_like(x[..., p:])], dim=-1)
        y = y0 + torch.fft.irfft(torch.fft.rfft(vin, n=nfft, dim=-1) * IR,
                                 n=nfft, dim=-1)[..., :Lb]
        y = y.reshape(B, N)
        if dbg:
            LpcSynthExact._probe("y (pre-deemph)", y)
        y = _deemph(y, PRE)
        if dbg:
            LpcSynthExact._probe("y (post-deemph)", y)
        if not self.upsample:
            return y
        up = torch.zeros(B, N * UP, device=y.device, dtype=y.dtype)
        up[:, ::UP] = y
        up = F.conv1d(up.unsqueeze(1), self.up_k.to(y.dtype),
                      padding=UP_COEFFS.size - 1)
        return up.squeeze(1)[:, : N * UP]


def synth_exact_interp(e: np.ndarray, a: np.ndarray, subframes: int = 4) -> np.ndarray:
    a = np.asarray(a, dtype=np.float64)
    e = np.asarray(e, dtype=np.float64).reshape(-1)
    nh = a.shape[0]
    assert e.size == nh * HOP
    k, ok = k_from_a(a, return_ok=True)
    k_prev = np.concatenate([k[:1], k[:-1]], axis=0)
    blk = HOP // subframes
    y = np.zeros(nh * HOP)
    hist = np.zeros(ORDER)
    for h in range(nh):
        for sfi in range(subframes):
            w = (sfi + 1) / subframes
            if ok[h]:
                ah = _step_up_np(k_prev[h] + w * (k[h] - k_prev[h]))
            else:
                ah = a[h, 1:]
            for n in range(blk):
                t = h * HOP + sfi * blk + n
                v = e[t] + float(ah @ hist)
                y[t] = v
                hist[1:] = hist[:-1]
                hist[0] = v
    from scipy.signal import lfilter
    y = lfilter([1.0], [1.0, -PRE], y)
    up = np.zeros(y.size * UP)
    up[::UP] = y
    return np.convolve(up, UP_COEFFS)[: y.size * UP]


def _step_up_np(k: np.ndarray) -> np.ndarray:
    p = len(k)
    a = np.zeros(p)
    for i in range(p):
        a_new = a.copy()
        a_new[i] = k[i]
        a_new[:i] = a[:i] - k[i] * a[i - 1::-1][:i]
        a = a_new
    return a

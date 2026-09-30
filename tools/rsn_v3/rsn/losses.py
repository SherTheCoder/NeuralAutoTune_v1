"""Training loss: L1 on the residual plus multi-resolution STFT on the
synthesized audio.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import torch
import torch.nn.functional as F

from . import dsp

FFT_SIZES = (256, 512, 1024, 2048)
FFT_SIZES_16K = (64, 128, 256, 512)

REF_EMA = 0.99
REF_JUMP = 10.0
SC_FLOOR = 0.1
EPS_REL = 1e-4


MAG_FLOOR = 1e-12


def stft_mag(x: torch.Tensor, n_fft: int) -> torch.Tensor:
    win = torch.hann_window(n_fft, device=x.device, dtype=x.dtype)
    X = torch.stft(x, n_fft=n_fft, hop_length=n_fft // 4, win_length=n_fft,
                   window=win, return_complex=True, center=True)
    return torch.sqrt(X.real ** 2 + X.imag ** 2 + MAG_FLOOR)


def mrstft(y, y_hat, ref_mag, sizes=FFT_SIZES, sc_floor=SC_FLOOR, eps_rel=EPS_REL):
    total = y.new_zeros(())
    for i, n_fft in enumerate(sizes):
        Y, Yh = stft_mag(y, n_fft), stft_mag(y_hat, n_fft)
        r = ref_mag[i].clamp_min(1e-12)
        num = torch.linalg.vector_norm(Y - Yh, dim=(-2, -1))
        den = torch.linalg.vector_norm(Y, dim=(-2, -1))
        numel = Y.shape[-1] * Y.shape[-2]
        floor = (sc_floor * (numel ** 0.5)) * r
        sc = (num / torch.maximum(den, floor.expand_as(den))).mean()
        eps = (eps_rel * r).clamp_min(MAG_FLOOR ** 0.5)
        mag = F.l1_loss(torch.log1p(Yh / eps), torch.log1p(Y / eps))
        total = total + sc + mag
    return total / len(sizes)


class RSNLoss(torch.nn.Module):

    def __init__(self, synth=None, w_mrstft: float = 0.5, sizes=FFT_SIZES,
                 sc_floor: float = SC_FLOOR, eps_rel: float = EPS_REL,
                 ema: float = REF_EMA, w_l1: float = 1.0, l1_mode: str = "plain",
                 w_dc: float = 0.0, w_wave: float = 1.0):
        super().__init__()
        self.synth = synth if synth is not None else dsp.LpcSynthExact()
        self.w = float(w_mrstft)
        self.w_l1 = float(w_l1)
        self.w_wave = float(w_wave)
        self.w_dc = float(w_dc)
        assert l1_mode in ("plain", "energy", "l2"), l1_mode
        self.l1_mode = l1_mode
        self.sizes = tuple(sizes)
        self.sc_floor = sc_floor
        self.eps_rel = eps_rel
        self.ema = ema
        self.register_buffer("ref_rms", torch.ones(()))
        self.register_buffer("ref_abs", torch.ones(()))
        self.register_buffer("ref_mag", torch.ones(len(self.sizes)))
        self.register_buffer("inited", torch.zeros((), dtype=torch.long))

    @torch.no_grad()
    def _update_ref(self, y, e_true=None, m=None):
        rms = y.pow(2).mean(-1).sqrt().median().clamp_min(1e-8)
        if not bool(torch.isfinite(rms)):
            return
        if e_true is not None:
            ab = ((e_true.abs() * m).sum() / m.sum().clamp_min(1.0)).clamp_min(1e-8)
        if self.inited == 0:
            self.ref_rms.copy_(rms)
            if e_true is not None:
                self.ref_abs.copy_(ab)
        elif self.training:
            self.ref_rms.mul_(self.ema).add_((1 - self.ema) * rms)
            if e_true is not None:
                self.ref_abs.mul_(self.ema).add_((1 - self.ema) * ab)
        yn = y / self.ref_rms
        mags = torch.stack([stft_mag(yn, n).pow(2).mean(dim=(-2, -1)).sqrt()
                            .median().clamp_min(1e-12) for n in self.sizes])
        mags = torch.nan_to_num(mags, nan=0.0, posinf=0.0, neginf=0.0)
        good = bool(torch.isfinite(mags).all()) and float(mags.min()) > 0.0
        if self.inited == 0:
            if good:
                self.ref_mag.copy_(mags)
            self.inited.fill_(1)
        elif self.training and good:
            ratio = mags / self.ref_mag.clamp_min(1e-12)
            if float(ratio.max()) > REF_JUMP:
                pass
            elif float(ratio.min()) < 1.0 / REF_JUMP:
                self.ref_mag.copy_(mags)
            else:
                self.ref_mag.mul_(self.ema).add_((1 - self.ema) * mags)
        if not bool(torch.isfinite(self.ref_mag).all()):
            self.ref_mag.copy_(torch.ones_like(self.ref_mag))

    def _recon(self, e_pred, e_true, m):
        d = e_pred - e_true
        if self.l1_mode == "l2":
            v = d.pow(2)
        elif self.l1_mode == "energy":
            w = e_true.abs()
            w = w / (w * m).sum(-1, keepdim=True).clamp_min(1e-8) * m.sum(-1, keepdim=True).clamp_min(1)
            v = d.abs() * w
        else:
            v = d.abs()
        return (v * m).sum() / m.sum().clamp_min(1.0)

    def forward(self, e_pred, e_true, a, valid=None):
        B, N = e_true.shape
        H = a.shape[1]
        if valid is None:
            m = torch.ones(B, N, device=e_true.device, dtype=e_true.dtype)
        else:
            m = valid.to(e_true.dtype).repeat_interleave(dsp.HOP, dim=1)[:, :N]
        e_p = torch.where(m > 0, e_pred, e_true)
        y_hat = self.synth(e_p, a)
        with torch.no_grad():
            y = self.synth(e_true, a)
            self._update_ref(y, e_true, m)
        l1 = self._recon(e_p, e_true, m) / self.ref_abs.clamp_min(1e-8)
        s = self.ref_rms.clamp_min(1e-8)
        wave = F.l1_loss(y_hat / s, y / s)
        ms = mrstft(y / s, y_hat / s, self.ref_mag, self.sizes, self.sc_floor, self.eps_rel)
        loss = self.w_l1 * l1 + self.w_wave * wave + self.w * ms
        parts = {"l1": float(l1), "wave": float(wave), "mrstft": float(ms)}
        if self.w_dc > 0:
            dc = ((e_p - e_true).view(B, H, dsp.HOP).mean(-1).abs() * (m.view(B, H, dsp.HOP)[..., 0])).sum() \
                / m.view(B, H, dsp.HOP)[..., 0].sum().clamp_min(1.0) / self.ref_abs.clamp_min(1e-8)
            loss = loss + self.w_dc * dc
            parts["dc"] = float(dc)
        return loss, parts


    def term_grads(self, e_pred, e_true, a, valid=None):
        out = {}
        for name, w_l1, w_wave, w_ms in (("l1", self.w_l1, 0.0, 0.0),
                                         ("wave", 0.0, self.w_wave, 0.0),
                                         ("mrstft", 0.0, 0.0, self.w)):
            e = e_pred.detach().clone().requires_grad_(True)
            keep = (self.w_l1, self.w_wave, self.w)
            self.w_l1, self.w_wave, self.w = w_l1, w_wave, w_ms
            try:
                loss, _ = self.forward(e, e_true, a, valid)
                if torch.isfinite(loss):
                    loss.backward()
                    g = e.grad
                    out[name] = (float(loss),
                                 float(g.norm()) if g is not None else 0.0,
                                 float(g.abs().max()) if g is not None else 0.0)
                else:
                    out[name] = (float("nan"), float("nan"), float("nan"))
            finally:
                self.w_l1, self.w_wave, self.w = keep
        return out

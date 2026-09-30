"""The RSN: 1x1 input conv, six dilated convs (k=5, d=1..32) with FiLM and tanh,
1x1 output conv. Also the quantization-aware training path and helpers for
the firmware's sliding window.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import torch
import torch.nn as nn

CIN = 4
CH = 96
EMB = 64
K = 5
DILATIONS = (1, 2, 4, 8, 16, 32)
RF = 1 + (K - 1) * sum(DILATIONS)
HOP = 80
WINDOW = 352
MAX_LOOKAHEAD = (K - 1) * sum(DILATIONS)

OUT_GAIN = 0.1      # initial output scale; calibrate_output() rescales it


def assert_rf():
    assert RF == 253, RF
    assert RF + HOP - 1 <= WINDOW, (
        f"RF {RF} + HOP {HOP} - 1 = {RF + HOP - 1} > window {WINDOW}")
    assert WINDOW % 32 == 0, "window must be divisible by every dilation"


def lookahead_pads(lookahead: int, dilations=DILATIONS, k: int = K):
    """(left, right) padding per layer for a total lookahead; every pad is a
    multiple of its dilation, which npu_rewrite needs."""
    L = int(lookahead)
    assert 0 <= L <= (k - 1) * sum(dilations), (L, (k - 1) * sum(dilations))
    right = {}
    rem = L
    for d in sorted(dilations, reverse=True):
        r = min(k - 1, rem // d)
        right[d] = r * d
        rem -= r * d
    assert rem == 0, (lookahead, rem)
    return [((k - 1) * d - right[d], right[d]) for d in dilations]


def _fq_act(x, lo: float, hi: float, stoch: bool = False):
    """Fake-quantize to asymmetric INT8 the way onnxruntime does (the range
    always contains 0). stoch: random rounding, so the network tolerates the
    NPU rounding slightly differently from onnxruntime."""
    lo, hi = min(float(lo), 0.0), max(float(hi), 0.0)
    s = max(hi - lo, 1e-8) / 255.0
    z = -128.0 - round(lo / s)
    xc = torch.clamp(x, lo, hi)
    r = torch.floor(xc / s + torch.rand_like(xc)) if stoch else torch.round(xc / s)
    q = (torch.clamp(r + z, -128.0, 127.0) - z) * s
    return xc + (q - xc).detach()


def _fq_w(w):
    """Per-channel symmetric weights in [-127, 127], as in the exported graph."""
    m = w.detach().abs().flatten(1).amax(1).clamp_min(1e-12).view(-1, *([1] * (w.dim() - 1)))
    s = m / 127.0
    q = torch.clamp(torch.round(w / s), -127.0, 127.0) * s
    return w + (q - w).detach()


class FiLM(nn.Module):

    def __init__(self, ch: int = CH, emb: int = EMB):
        super().__init__()
        self.gamma = nn.Linear(emb, ch)
        self.delta = nn.Linear(emb, ch)
        nn.init.zeros_(self.gamma.weight)
        nn.init.ones_(self.gamma.bias)
        nn.init.zeros_(self.delta.weight)
        nn.init.zeros_(self.delta.bias)

    def forward(self, x, emb):
        g = self.gamma(emb).view(-1, x.shape[1], 1, 1)
        d = self.delta(emb).view(-1, x.shape[1], 1, 1)
        return g * x + d


def deployable(ch, dilations, lookahead):
    dil = tuple(dilations)
    rf = 1 + (K - 1) * sum(dil)
    return (ch == CH and rf + HOP - 1 <= WINDOW
            and all(WINDOW % d == 0 for d in dil)
            and 0 <= int(lookahead) <= (K - 1) * sum(dil))


class RSNv2(nn.Module):

    def __init__(self, n_embeddings: int = 1, ch: int = CH, dilations=DILATIONS,
                 lookahead: int = 0, causal: bool = True):
        super().__init__()
        self.dilations = tuple(dilations)
        if not causal:
            lookahead = (K - 1) * sum(self.dilations) // 2
        self.lookahead = int(lookahead)
        self.ch = ch
        self.rf = 1 + (K - 1) * sum(self.dilations)
        self.is_deployed_arch = deployable(ch, self.dilations, self.lookahead)
        if self.dilations == DILATIONS and ch == CH:
            assert_rf()
        self.pads = lookahead_pads(self.lookahead, self.dilations)
        self.emb = nn.Embedding(n_embeddings, EMB)
        nn.init.normal_(self.emb.weight, 0.0, 0.01)
        self.in_proj = nn.Conv2d(CIN, ch, 1)
        self.convs = nn.ModuleList([
            nn.Conv2d(ch, ch, (1, K), dilation=(1, d)) for d in self.dilations])
        self.films = nn.ModuleList([FiLM(ch) for _ in self.dilations])
        self.out_proj = nn.Conv2d(ch, 1, 1)
        with torch.no_grad():
            self.out_proj.weight.mul_(OUT_GAIN)
            self.out_proj.bias.zero_()
        self.qat = None
        self.qat_stoch = False
        self.qat_calibrating = False
        self.qat_obs = {}

    def forward(self, x, emb_idx):
        e = emb_idx if emb_idx.is_floating_point() else self.emb(emb_idx)
        if self.qat is not None or self.qat_calibrating:
            return self._forward_qat(x, e)
        h = self.in_proj(x)
        for conv, film, (pl, pr) in zip(self.convs, self.films, self.pads):
            h = conv(nn.functional.pad(h, (pl, pr, 0, 0)))
            h = torch.tanh(film(h, e))
        return self.out_proj(h)

    QAT_TENSORS = (["input", "in_proj"]
                   + [f"{k}{i}" for i in range(6) for k in ("gamma", "delta", "conv", "mul", "add", "tanh")]
                   + ["out"])

    def _fq(self, name, t):
        if self.qat_calibrating:
            v = t.detach().flatten()
            if v.numel() > 200_000:
                v = v[torch.randint(v.numel(), (200_000,), device=v.device)]
            lo, hi = torch.quantile(v.float(), torch.tensor([1e-4, 1 - 1e-4], device=v.device))
            self.qat_obs.setdefault(name, []).append((float(lo), float(hi)))
            return t
        lo, hi = self.qat[name]
        return _fq_act(t, lo, hi, stoch=self.training and self.qat_stoch)

    def _forward_qat(self, x, e):
        F_ = nn.functional
        h = self._fq("input", x)
        h = self._fq("in_proj", F_.conv2d(h, _fq_w(self.in_proj.weight), self.in_proj.bias))
        for i, (conv, film, (pl, pr)) in enumerate(zip(self.convs, self.films, self.pads)):
            g = self._fq(f"gamma{i}", film.gamma(e).view(-1, self.ch, 1, 1))
            d = self._fq(f"delta{i}", film.delta(e).view(-1, self.ch, 1, 1))
            c = F_.conv2d(F_.pad(h, (pl, pr, 0, 0)), _fq_w(conv.weight), conv.bias,
                          dilation=conv.dilation)
            c = self._fq(f"conv{i}", c)
            m = self._fq(f"mul{i}", g * c)
            a = self._fq(f"add{i}", m + d)
            h = self._fq(f"tanh{i}", torch.tanh(a))
        return self._fq("out", F_.conv2d(h, _fq_w(self.out_proj.weight), self.out_proj.bias))

    def qat_begin_calibration(self):
        self.qat, self.qat_calibrating, self.qat_obs = None, True, {}

    def qat_end_calibration(self):
        import statistics
        self.qat = {k: (min(statistics.median(a for a, _ in v), 0.0),
                        max(statistics.median(b for _, b in v), 0.0))
                    for k, v in self.qat_obs.items()}
        self.qat_calibrating = False
        return self.qat

    def arch(self):
        return {"ch": self.ch, "dilations": list(self.dilations),
                "lookahead": self.lookahead}

    @property
    def causal(self):
        return self.lookahead == 0

    def param_count(self):
        return sum(p.numel() for p in self.parameters() if p.requires_grad)


def from_arch(arch: dict, n_embeddings: int):
    arch = dict(arch or {})
    la = arch.get("lookahead")
    if la is None:
        la = 0 if arch.get("causal", True) else (K - 1) * sum(arch.get("dilations", DILATIONS)) // 2
    return RSNv2(n_embeddings=n_embeddings, ch=arch.get("ch", CH),
                 dilations=tuple(arch.get("dilations", DILATIONS)), lookahead=int(la))


def output_slice(lookahead: int, window: int = WINDOW, hop: int = HOP):
    """The outputs the firmware keeps from each window."""
    return window - lookahead - hop, window - lookahead


def sliding_window_equiv(model, x, emb_idx, window=WINDOW, hop=HOP):
    L = model.lookahead
    full = model(x, emb_idx)
    T = x.shape[-1]
    out = torch.zeros_like(full)
    first = ((window + hop - 1) // hop) * hop
    ends = list(range(first, T + 1, hop))
    assert ends, "sequence shorter than one window"
    o0, o1 = output_slice(L, window, hop)
    for end in ends:
        y = model(x[..., end - window:end], emb_idx)
        out[..., end - L - hop:end - L] = y[..., o0:o1]
    valid = slice(ends[0] - L - hop, ends[-1] - L)
    return full[..., valid], out[..., valid]


CAL_RATIO = 0.02


@torch.no_grad()
def calibrate_output(model, x, u, y, valid=None, target_ratio: float = CAL_RATIO,
                     max_gain: float = 1e6):
    e = model(x, u).squeeze(1).squeeze(1)
    if valid is not None:
        m = valid.to(e.dtype).repeat_interleave(HOP, dim=1)[:, :e.shape[-1]]
        es = float((e * m).pow(2).sum().div(m.sum().clamp_min(1.0)).sqrt())
        ys = float((y * m).pow(2).sum().div(m.sum().clamp_min(1.0)).sqrt())
    else:
        es, ys = float(e.std()), float(y.std())
    if es <= 0 or not (ys > 0):
        return 1.0
    g = min(max(target_ratio * ys / es, 1e-4), max_gain)
    model.out_proj.weight.mul_(g)
    model.out_proj.bias.mul_(g)
    return g

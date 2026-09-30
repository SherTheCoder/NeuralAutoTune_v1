"""Multi-resolution and multi-period discriminators for the adversarial stage.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import torch
import torch.nn as nn
import torch.nn.functional as F

from . import dsp


def _wn(m):
    return nn.utils.weight_norm(m)


class MRDBlock(nn.Module):
    def __init__(self, n_fft: int, ch: int = 32):
        super().__init__()
        self.n_fft = n_fft
        self.hop = n_fft // 4
        self.convs = nn.ModuleList([
            _wn(nn.Conv2d(1, ch, (3, 9), padding=(1, 4))),
            _wn(nn.Conv2d(ch, ch, (3, 9), stride=(1, 2), padding=(1, 4))),
            _wn(nn.Conv2d(ch, ch, (3, 9), stride=(1, 2), padding=(1, 4))),
            _wn(nn.Conv2d(ch, ch, (3, 9), stride=(1, 2), padding=(1, 4))),
            _wn(nn.Conv2d(ch, ch, (3, 3), padding=(1, 1))),
        ])
        self.post = _wn(nn.Conv2d(ch, 1, (3, 3), padding=(1, 1)))

    def forward(self, x):
        win = torch.hann_window(self.n_fft, device=x.device, dtype=x.dtype)
        X = torch.stft(x, self.n_fft, self.hop, window=win, return_complex=True, center=True)
        h = torch.log1p(X.abs()).unsqueeze(1)
        feats = []
        for c in self.convs:
            h = F.leaky_relu(c(h), 0.1)
            feats.append(h)
        h = self.post(h)
        feats.append(h)
        return h.flatten(1), feats


class MPDBlock(nn.Module):
    def __init__(self, period: int, ch=(32, 64, 128, 128)):
        super().__init__()
        self.p = period
        layers, cin = [], 1
        for i, c in enumerate(ch):
            layers.append(_wn(nn.Conv2d(cin, c, (5, 1), stride=(3, 1), padding=(2, 0))))
            cin = c
        self.convs = nn.ModuleList(layers)
        self.post = _wn(nn.Conv2d(cin, 1, (3, 1), padding=(1, 0)))

    def forward(self, x):
        B, T = x.shape
        pad = (-T) % self.p
        if pad:
            x = F.pad(x, (0, pad), mode="reflect")
        h = x.view(B, 1, -1, self.p)
        feats = []
        for c in self.convs:
            h = F.leaky_relu(c(h), 0.1)
            feats.append(h)
        h = self.post(h)
        feats.append(h)
        return h.flatten(1), feats


class Discriminators(nn.Module):
    def __init__(self, ffts=(256, 512, 1024), periods=(2, 3, 5, 7, 11)):
        super().__init__()
        self.ds = nn.ModuleList([MRDBlock(n) for n in ffts] + [MPDBlock(p) for p in periods])

    def forward(self, x):
        return [d(x) for d in self.ds]


class GANStage:

    def __init__(self, dev, w_adv=0.05, w_fm=1.0, lr=2e-4, synth16=None):
        self.D = Discriminators().to(dev)
        self.opt = torch.optim.AdamW(self.D.parameters(), lr=lr, betas=(0.8, 0.99), weight_decay=1e-2)
        self.w_adv, self.w_fm = float(w_adv), float(w_fm)
        self.synth = synth16 if synth16 is not None else dsp.LpcSynthExact(upsample=False).to(dev)
        self.last_d = float("nan")

    def _render(self, e_pred, e_true, a, valid):
        m = valid.to(e_true.dtype).repeat_interleave(dsp.HOP, dim=1)[:, : e_true.shape[1]]
        e_p = torch.where(m > 0, e_pred, e_true)
        with torch.no_grad():
            y = self.synth(e_true, a)
            s = y.pow(2).mean(-1, keepdim=True).sqrt().clamp_min(1e-6)
        y_hat = self.synth(e_p, a)
        return y_hat / s, y / s

    def generator_loss(self, e_pred, e_true, a, valid):
        y_hat, y = self._render(e_pred, e_true, a, valid)
        outs_hat = self.D(y_hat)
        with torch.no_grad():
            outs_ref = self.D(y)
        adv = sum(((1.0 - o) ** 2).mean() for o, _ in outs_hat) / len(outs_hat)
        fm = 0.0
        for (_, fh), (_, fr) in zip(outs_hat, outs_ref):
            fm = fm + sum(F.l1_loss(h, r) for h, r in zip(fh, fr)) / len(fh)
        fm = fm / len(outs_hat)
        return self.w_adv * adv + self.w_fm * fm, {"adv": float(adv), "fm": float(fm), "d": self.last_d}

    def discriminator_step(self, e_pred_detached, e_true, a, valid):
        with torch.no_grad():
            y_hat, y = self._render(e_pred_detached, e_true, a, valid)
        outs_hat = self.D(y_hat.detach())
        outs_ref = self.D(y)
        loss = sum(((1.0 - o) ** 2).mean() + (oh ** 2).mean()
                   for (o, _), (oh, _) in zip(outs_ref, outs_hat)) / len(outs_ref)
        self.opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(self.D.parameters(), 10.0)
        self.opt.step()
        self.last_d = float(loss)
        return float(loss)

    def state_dict(self):
        return {"D": self.D.state_dict(), "opt": self.opt.state_dict()}

    def load_state_dict(self, sd):
        self.D.load_state_dict(sd["D"])
        self.opt.load_state_dict(sd["opt"])

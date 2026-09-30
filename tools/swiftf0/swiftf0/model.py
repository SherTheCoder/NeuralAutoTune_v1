"""The pitch network: a stack of 1x5 convolutions over the 20-cent grid.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import torch
import torch.nn as nn

NBINS_IN, NBINS_OUT = 480, 360
CH = 60
DILATIONS = (1, 2, 4, 8, 16, 32)

class SwiftF0(nn.Module):
    def __init__(self, ch: int = CH):
        super().__init__()
        layers, cin = [], 1
        for d in DILATIONS:
            layers += [
                nn.Conv2d(cin, ch, (1, 5), padding=(0, 2 * d), dilation=(1, d)),
                nn.BatchNorm2d(ch),
                nn.ReLU(inplace=True),
            ]
            cin = ch
        self.backbone = nn.Sequential(*layers)
        self.pitch_head = nn.Conv2d(ch, 1, 1)
        self.voice_fc1 = nn.Linear(ch, 32)
        self.voice_fc2 = nn.Linear(32, 1)

    def forward(self, x):
        h = self.backbone(x)
        pitch = self.pitch_head(h)[:, 0, 0, :NBINS_OUT]
        v = h.mean(dim=3)[:, :, 0]
        voice = self.voice_fc2(torch.relu(self.voice_fc1(v)))
        return pitch, voice

def param_count(m: nn.Module) -> int:
    return sum(p.numel() for p in m.parameters() if p.requires_grad)

if __name__ == "__main__":
    m = SwiftF0()
    n = param_count(m)
    p, v = m(torch.zeros(2, 1, 1, NBINS_IN))
    print(f"SwiftF0 ch={CH}: {n:,} params (target ~95,842); "
          f"pitch {tuple(p.shape)}, voice {tuple(v.shape)}")
    rf = 1 + 4 * sum(DILATIONS)
    print(f"receptive field {rf} bins = {rf * 20 / 1200:.1f} octaves")

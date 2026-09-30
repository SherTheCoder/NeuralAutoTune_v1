"""Train the pitch network.

    python -m swiftf0.train --cache <cache> --out <dir>

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from .data import FrameDataset
from .eval import evaluate_model
from .model import NBINS_OUT, SwiftF0, param_count

SIGMA_BINS = 1.25
VOICE_W = 0.5

def soft_targets(b, device):
    grid = torch.arange(NBINS_OUT, device=device, dtype=torch.float32)
    t = torch.exp(-0.5 * ((grid[None] - b[:, None]) / SIGMA_BINS) ** 2)
    t = t * (b[:, None] >= 0)
    return t / t.sum(dim=1, keepdim=True).clamp_min(1e-9)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--steps", type=int, default=30000)
    ap.add_argument("--batch", type=int, default=256)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--warmup", type=int, default=500)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--val-singers", default="abjones,amy,ADIZ,SAMF")
    ap.add_argument("--eval-every", type=int, default=2000)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)

    dev = ("cuda" if torch.cuda.is_available() else
           "mps" if torch.backends.mps.is_available() else "cpu")
    val_singers = set(a.val_singers.split(","))
    tr = FrameDataset(a.cache, exclude=val_singers, augment=True)
    va = FrameDataset(a.cache, singers=val_singers, augment=False)
    print(f"device {dev} | train {len(tr)} frames / {len(tr.singers)} singers"
          f" | val {len(va)} frames / {sorted(val_singers)}")

    model = SwiftF0().to(dev)
    print(f"params: {param_count(model):,}")
    opt = torch.optim.Adam(model.parameters(), lr=a.lr)
    sched = lambda s: (s + 1) / a.warmup if s < a.warmup else \
        0.01 + 0.99 * 0.5 * (1 + math.cos(math.pi * (s - a.warmup)
                                          / max(1, a.steps - a.warmup)))
    dl = DataLoader(tr, batch_size=a.batch, shuffle=True, drop_last=True,
                    num_workers=a.workers, persistent_workers=a.workers > 0)

    best_rpa, step, t0 = -1.0, 0, time.time()
    hist = []
    while step < a.steps:
        for feat, b, voiced in dl:
            if step >= a.steps:
                break
            for g in opt.param_groups:
                g["lr"] = a.lr * sched(step)
            feat = feat.to(dev)[:, None, None, :]
            b, voiced = b.to(dev), voiced.to(dev)
            logits, vlogit = model(feat)
            tgt = soft_targets(b, dev)
            nv = (b >= 0).sum().clamp_min(1)
            pitch_loss = -(tgt * F.log_softmax(logits, dim=1)).sum() / nv
            voice_loss = F.binary_cross_entropy_with_logits(
                vlogit[:, 0], voiced)
            loss = pitch_loss + VOICE_W * voice_loss
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            step += 1

            if step % 200 == 0:
                print(f"step {step:6d}/{a.steps} loss {loss.item():.4f} "
                      f"(pitch {pitch_loss.item():.4f} voice "
                      f"{voice_loss.item():.4f}) "
                      f"lr {opt.param_groups[0]['lr']:.2e} "
                      f"{(time.time() - t0):.0f}s", flush=True)
            if step % a.eval_every == 0 or step == a.steps:
                m = evaluate_model(model, va, dev, batch=a.batch)
                hist.append({"step": step, **m})
                (a.out / "history.json").write_text(json.dumps(hist, indent=1))
                print(f"  eval: RPA50 {m['rpa50']:.2%} RCA50 {m['rca50']:.2%}"
                      f" | median |err| {m['med_cents']:.1f}c"
                      f" | voicing acc {m['voice_acc']:.2%}", flush=True)
                torch.save(model.state_dict(), a.out / "last.pt")
                if m["rpa50"] > best_rpa:
                    best_rpa = m["rpa50"]
                    torch.save(model.state_dict(), a.out / "best.pt")
                    (a.out / "best.json").write_text(json.dumps(
                        {"step": step, **m}, indent=1))
    print(f"done. best RPA@50c {best_rpa:.2%} -> {a.out}/best.pt")

if __name__ == "__main__":
    main()

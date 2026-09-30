"""Fit a singer embedding for a new voice with the network frozen.

    python -m rsn.calibrate --ckpt <ckpt> --cache <cache> --singer <name> --out <json>

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
import torch

from . import data as D
from . import dsp
from .losses import RSNLoss
from .model import EMB, from_arch
from .train import rho_valid

SEG = 300


def singer_items(cache, singer, lookahead, exclude=None):
    um = json.loads((Path(cache) / "utt_map.json").read_text())
    u2s = {int(k): v for k, v in um["utt_singer"].items()}
    bad = set(json.loads(Path(exclude).read_text())["bad"]) if exclude else set()
    items, _ = D.load_cache(cache, lookahead=lookahead)
    return [(i, it) for i, it in enumerate(items)
            if u2s[int(it[3])] == singer and i not in bad and it[2].shape[0] >= SEG]


@torch.no_grad()
def item_rho(model, items, emb, dev):
    rs = []
    for x, y, a, u, v in items:
        xt = torch.from_numpy(x).float()[None, :, None, :].to(dev)
        e = (torch.tensor([u], device=dev) if emb is None
             else emb.view(1, EMB).to(dev))
        yh = model(xt, e).view(1, -1).float().cpu()
        rs.append(rho_valid(yh, torch.from_numpy(y)[None].float(),
                            torch.from_numpy(v)[None]))
    return float(np.median(rs)), rs


def calibrate(model, crit, items, dev, steps=500, lr=5e-4, batch=16, seed=0, init=None,
              log=None):
    for p in model.parameters():
        p.requires_grad_(False)
    torch.manual_seed(seed)
    dl = torch.utils.data.DataLoader(D.SegmentDataset(items, seg_hops=SEG), batch_size=batch,
                                     shuffle=True, collate_fn=D.collate, drop_last=False)
    it = iter(dl)
    vec = (init.clone() if init is not None else torch.zeros(EMB)).to(dev).requires_grad_(True)
    opt = torch.optim.Adam([vec], lr=lr)
    hist = []
    for s in range(steps):
        for g in opt.param_groups:
            g["lr"] = 1e-6 + 0.5 * (lr - 1e-6) * (1 + math.cos(math.pi * s / steps))
        try:
            bt = next(it)
        except StopIteration:
            it = iter(dl)
            bt = next(it)
        x, y, a, u, v = (t.to(dev) for t in bt)
        e = model(x, vec.view(1, EMB).expand(x.shape[0], EMB)).squeeze(1).squeeze(1)
        loss, parts = crit(e.float(), y, a, v)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        if torch.isfinite(vec.grad).all():
            opt.step()
        hist.append(float(loss))
        if log and (s % 100 == 0 or s == steps - 1):
            log(f"    step {s:4d}  loss {float(loss):.4f}  (L1 {parts['l1']:.3f} wave "
                f"{parts['wave']:.3f} MRSTFT {parts['mrstft']:.3f})")
    return vec.detach().cpu(), hist


def int8_sym(vec):
    s = float(vec.abs().max()) / 127.0 if float(vec.abs().max()) > 0 else 1.0
    q = torch.clamp(torch.round(vec / s), -127, 127).to(torch.int8)
    return q, s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", required=True)
    ap.add_argument("--cache", required=True)
    ap.add_argument("--singer", required=True)
    ap.add_argument("--exclude", default=None)
    ap.add_argument("--calib-secs", dest="calib_secs", type=float, default=240.0,
                    help="seconds of the singer's audio to calibrate on (3-5 min is plenty)")
    ap.add_argument("--steps", type=int, default=500)
    ap.add_argument("--lr", type=float, default=5e-4)
    ap.add_argument("--test-n", dest="test_n", type=int, default=40)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    dev = torch.device(a.device)

    ck = torch.load(a.ckpt, map_location=dev, weights_only=False)
    model = from_arch(ck.get("arch"), ck["n_emb"]).to(dev).eval()
    model.load_state_dict(ck["model"])
    if ck.get("qat"):
        model.qat = {k: tuple(v) for k, v in ck["qat"].items()}
    L = model.lookahead
    meta = ck.get("meta", {})
    emb_mode = meta.get("emb_mode", "file")
    held = a.singer in (meta.get("holdout") or [])
    crit = RSNLoss(dsp.LpcSynthExact(interp="firmware")).to(dev)
    crit.train()

    t0 = time.time()
    its = singer_items(a.cache, a.singer, L, a.exclude)
    rng = np.random.default_rng(0)
    order = rng.permutation(len(its))
    calib, test, secs = [], [], 0.0
    for j in order:
        i, it = its[j]
        if secs < a.calib_secs:
            calib.append(it)
            secs += it[2].shape[0] * dsp.HOP / dsp.FS16
        else:
            test.append(it)
    test = test[: a.test_n]
    if emb_mode == "singer":
        names = meta.get("singers") or []
        sid = names.index(a.singer) if a.singer in names else None
        test_own = [(x, y, aa, sid, v) for x, y, aa, _, v in test] if sid is not None else None
    else:
        test_own = test
    print(f"{a.ckpt}: L={L}, emb={emb_mode}, singer {a.singer} "
          f"({'HELD OUT of training' if held else 'seen in training'}); "
          f"{len(its)} items -> calibrate on {len(calib)} ({secs:.0f} s), test on {len(test)} "
          f"({time.time() - t0:.0f}s load)")

    table = model.emb.weight.detach().cpu()
    hold = set(meta.get("holdout") or [])
    if hold and emb_mode == "file":
        um = json.loads((Path(a.cache) / "utt_map.json").read_text())
        rows = [int(k) for k, v in um["utt_singer"].items() if v not in hold]
    elif hold and emb_mode == "singer":
        rows = [i for i, n in enumerate(meta.get("singers") or []) if n not in hold]
    else:
        rows = list(range(table.shape[0]))
    mean = table[rows].mean(0)
    print(f"  'mean' baseline over {len(rows)} trained embedding rows")
    print("  calibrating (network frozen) ...")
    vec, hist = calibrate(model, crit, calib, dev, steps=a.steps, lr=a.lr, init=mean,
                          log=print)
    q, sc = int8_sym(vec)
    vec_q = q.float() * sc

    res = {"singer": a.singer, "held_out": held, "emb_mode": emb_mode, "lookahead": L,
           "calib_items": len(calib), "calib_secs": secs, "test_items": len(test)}
    for name, e in (("calibrated", vec), ("calibrated_int8", vec_q), ("mean", mean)):
        res[f"rho_{name}"], _ = item_rho(model, test, e, dev)
    if test_own is not None and not held:
        res["rho_own"], _ = item_rho(model, test_own, None, dev)
    print("  rho on the singer's test items: " + "  ".join(
        f"{k[4:]} {v:.3f}" for k, v in res.items() if k.startswith("rho_")))
    res["vector"] = vec.tolist()
    res["int8"] = q.tolist()
    res["int8_scale"] = sc
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1))
        print(f"-> {a.out}")


if __name__ == "__main__":
    main()

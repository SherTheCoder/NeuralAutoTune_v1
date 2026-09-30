"""Train the RSN.

    python -m rsn.train --cache <cache> --out <dir>

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import argparse
import json
import math
import time
from collections import deque
from pathlib import Path

import numpy as np
import torch

from . import data as D
from . import dsp
from .losses import FFT_SIZES, FFT_SIZES_16K, RSNLoss
from .model import (CAL_RATIO, RSNv2, WINDOW, assert_rf, from_arch,
                    calibrate_output)

CLIP_K = 4.0
CLIP_WIN = 200
ROGUE_MULT = 1000.0
ROGUE_REPORT = 3


def lr_at(step, base=1e-4, warmup=1000, total=100_000, final=1e-6):
    if step < warmup:
        return base * (step + 1) / warmup
    p = (step - warmup) / max(1, total - warmup)
    return final + 0.5 * (base - final) * (1 + math.cos(math.pi * min(p, 1.0)))


def clip_threshold(recent, fixed):
    if fixed > 0:
        return fixed
    if not recent:
        return float("inf")
    return CLIP_K * float(np.median(recent))


def _grad_scaler(enabled):
    try:
        return torch.amp.GradScaler("cuda", enabled=enabled)
    except (AttributeError, TypeError):
        return torch.cuda.amp.GradScaler(enabled=enabled)


def _forever(dl):
    while True:
        for b in dl:
            yield b


def embedding_ids(items, meta, mode="file"):
    return items


def build_model_and_loss(args, n_utt, dev):
    ch = getattr(args, "probe_width", 0) or None
    dil = getattr(args, "probe_dilations", None)
    kw = {"lookahead": int(getattr(args, "lookahead", 0))}
    if ch:
        kw["ch"] = ch
    if dil:
        kw["dilations"] = tuple(int(d) for d in dil)
    model = RSNv2(n_embeddings=n_utt, **kw).to(dev)
    at16 = bool(getattr(args, "mrstft_16k", False))
    synth = dsp.LpcSynthExact(upsample=not at16,
                              interp=getattr(args, "synth_interp", "firmware"),
                              fp64=bool(getattr(args, "synth_fp64", False))).to(dev)
    crit = RSNLoss(synth, w_mrstft=args.w_mrstft,
                   sizes=(FFT_SIZES_16K if at16 else FFT_SIZES),
                   w_l1=args.w_l1, l1_mode=args.l1_mode,
                   w_dc=getattr(args, "w_dc", 0.0),
                   w_wave=getattr(args, "w_wave", 1.0)).to(dev)
    cfg = {"w_l1": args.w_l1, "w_wave": getattr(args, "w_wave", 1.0),
           "l1_mode": args.l1_mode, "w_mrstft": args.w_mrstft,
           "mrstft_16k": at16, "w_dc": getattr(args, "w_dc", 0.0),
           "synth_interp": getattr(args, "synth_interp", "firmware"),
           "augment_db": getattr(args, "augment_db", None),
           "gan": bool(getattr(args, "gan", False))}
    if not model.is_deployed_arch:
        print("  *** PROBE ARCHITECTURE -- NOT DEPLOYABLE ***  "
              f"{model.arch()}  (export.py will refuse this checkpoint)")
    return model, crit, model.arch(), cfg


def load_dataset(args):
    if args.cache:
        items, meta = D.load_cache(args.cache, lookahead=args.lookahead)
        seg = getattr(args, "seg_hops", 300)
        orig = [i for i, it in enumerate(items) if it[2].shape[0] >= seg]
        keep = [items[i] for i in orig]
        if len(keep) < len(items):
            drop = len(items) - len(keep)
            if not keep or len(keep) < 0.2 * len(items):
                raise SystemExit(f"\n{drop} of {len(items)} cached items are shorter than "
                                 f"--seg_hops {seg}; lower --seg_hops or re-prep with a "
                                 f"larger --max-secs.")
            print(f"  dropped {drop}/{len(items)} items shorter than --seg_hops {seg}")
            items = keep
        n_val = max(1, int(len(items) * meta.get("val_frac", 0.05)))
        rng = np.random.default_rng(0)
        idx = rng.permutation(len(items))
        bad = set()
        if getattr(args, "exclude", None):
            ex = json.loads(Path(args.exclude).read_text())
            bad = set(int(i) for i in ex["bad"])
        val = [items[i] for i in idx[:n_val] if orig[i] not in bad]
        train = [items[i] for i in idx[n_val:] if orig[i] not in bad]
        if bad:
            print(f"  excluded {len(bad)} diverging-filter items ({args.exclude}): "
                  f"{n_val - len(val)} from val, {len(idx) - n_val - len(train)} from train")
        emb_mode = getattr(args, "emb", "file") or "file"
        holdout = list(getattr(args, "holdout", None) or [])
        if emb_mode in ("singer", "none") or holdout:
            um = json.loads((Path(args.cache) / "utt_map.json").read_text())
            u2s = {int(k): v for k, v in um["utt_singer"].items()}
            names = sorted(set(u2s.values()))
            unknown = [h for h in holdout if h not in names]
            if unknown:
                raise SystemExit(f"--holdout {unknown}: not singers in this cache ({names})")
            if holdout:
                nt, nv = len(train), len(val)
                train = [it for it in train if u2s[int(it[3])] not in holdout]
                val = [it for it in val if u2s[int(it[3])] not in holdout]
                print(f"  HELD OUT singers {holdout}: -{nt - len(train)} train, "
                      f"-{nv - len(val)} val items (never seen; test with rsn.calibrate)")
            if emb_mode == "none":
                train = [(it[0], it[1], it[2], 0, it[4]) for it in train]
                val = [(it[0], it[1], it[2], 0, it[4]) for it in val]
                print("  ONE shared embedding (control)")
            if emb_mode == "singer":
                sid = {n: i for i, n in enumerate(names)}
                remap = lambda it: (it[0], it[1], it[2], sid[u2s[int(it[3])]], it[4])
                train = [remap(it) for it in train]
                val = [remap(it) for it in val]
                print(f"  embeddings per SINGER: {len(names)} ({', '.join(names)})")
            meta = dict(meta)
            meta.update({"emb_mode": emb_mode, "holdout": holdout, "singers": names,
                         "n_emb": {"singer": len(names), "none": 1}.get(
                             emb_mode, meta.get("n_utterances"))})
        gb = sum(it[0].nbytes + it[1].nbytes + it[2].nbytes for it in train + val) / 1e9
        print(f"cache {args.cache}: {len(train)} train / {len(val)} val items, "
              f"RES_SCALE {meta['res_scale']:.1f}, {meta['n_singers']} singers, "
              f"lookahead {args.lookahead}, {gb:.2f} GB resident")
        return train, val, meta
    items, rs = D.build_synthetic(n_pairs=args.pairs, dur_s=args.dur,
                                  lookahead=args.lookahead, return_scale=True)
    return items, items[:1], {"res_scale": rs, "fmt": D.FMT, "synthetic": True}


@torch.no_grad()
def rho_valid(e_pred, e_true, valid):
    m = valid.to(e_true.dtype).repeat_interleave(dsp.HOP, dim=1)[:, : e_true.shape[1]]
    p = e_pred * m
    t = e_true * m
    num = (p * t).sum(-1)
    den = (p.pow(2).sum(-1) * t.pow(2).sum(-1)).sqrt().clamp_min(1e-12)
    return float((num / den).median())


def evaluate(model, crit, dl, dev, amp):
    model.eval()
    crit.eval()
    tot, nb, nnan, rhos = 0.0, 0, 0, []
    with torch.no_grad():
        for x, y, a, u, v in dl:
            x, y, a, u, v = x.to(dev), y.to(dev), a.to(dev), u.to(dev), v.to(dev)
            with torch.autocast(dev.type, enabled=amp):
                e = model(x, u).squeeze(1).squeeze(1)
            loss, _ = crit(e.float(), y, a, v)
            fl = float(loss)
            if math.isfinite(fl):
                tot += fl
                nb += 1
            else:
                nnan += 1
            rhos.append(rho_valid(e.float(), y, v))
    model.train()
    crit.train()
    if nnan:
        print(f"  [val] {nnan} non-finite batches excluded from the mean")
    return tot / max(nb, 1), float(np.median(rhos)) if rhos else float("nan")


def run(args):
    assert_rf()
    dev = torch.device(args.device)
    amp = bool(args.amp and dev.type == "cuda")
    torch.manual_seed(0)

    train_items, val_items, meta = load_dataset(args)
    n_utt = max(max(int(it[3]) for it in train_items + val_items) + 1,
                int(meta.get("n_emb") or 0))
    aug = tuple(args.augment_db) if args.augment_db else None
    ds = D.SegmentDataset(train_items, seg_hops=args.seg_hops, augment_db=aug)
    vs = D.SegmentDataset(val_items, seg_hops=args.seg_hops)
    dl = torch.utils.data.DataLoader(
        ds, batch_size=args.batch, shuffle=True, collate_fn=D.collate,
        drop_last=True, num_workers=args.workers, persistent_workers=args.workers > 0)
    vdl = torch.utils.data.DataLoader(vs, batch_size=args.batch, shuffle=False,
                                      collate_fn=D.collate)

    model, crit, arch, loss_cfg = build_model_and_loss(args, n_utt, dev)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    scaler = _grad_scaler(amp)

    gan = None
    if args.gan:
        from . import gan as G
        gan = G.GANStage(dev, w_adv=args.w_adv, w_fm=args.w_fm, lr=args.lr_d,
                         synth16=dsp.LpcSynthExact(upsample=False,
                                                   interp=args.synth_interp).to(dev))

    step, hist, resumed_g = 0, [], []
    if args.resume and Path(args.resume).exists():
        ck = torch.load(args.resume, map_location=dev, weights_only=False)
        bad = [k for k, v in ck["model"].items()
               if torch.is_floating_point(v) and not torch.isfinite(v).all()]
        if bad:
            raise SystemExit(f"\n{args.resume} holds non-finite weights ({bad[0]}) -- the "
                             f"wreck of a diverged run. Move it aside and start fresh.")
        ck_emb = ck.get("n_emb")
        ck_rs = (ck.get("meta") or {}).get("res_scale")
        why = []
        if ck_emb is not None and ck_emb != n_utt:
            why.append(f"{ck_emb} embeddings, this cache has {n_utt}")
        if ck_rs is not None and abs(ck_rs - meta["res_scale"]) > 1e-6:
            why.append(f"RES_SCALE {ck_rs:.1f}, this cache measures {meta['res_scale']:.1f}")
        if ck.get("arch") and ck["arch"] != arch and not args.init_from:
            why.append(f"arch {ck['arch']} != {arch}")
        if why:
            raise SystemExit(f"\n{args.resume} does not belong to this run "
                             f"({'; '.join(why)}). Start fresh (or pass it as "
                             f"--init-from to reuse the weights only).")
        model.load_state_dict(ck["model"])
        opt.load_state_dict(ck["opt"])
        if "crit" in ck:
            crit.load_state_dict(ck["crit"])
        if amp and ck.get("scaler"):
            scaler.load_state_dict(ck["scaler"])
        if gan is not None and ck.get("gan"):
            gan.load_state_dict(ck["gan"])
        step = ck["step"]
        hist = ck.get("hist", [])
        resumed_g = ck.get("g_recent", [])
        print(f"resumed from {args.resume} at step {step}")
    elif args.init_from and Path(args.init_from).exists():
        ck = torch.load(args.init_from, map_location=dev, weights_only=False)
        sd = {k: v for k, v in ck["model"].items()
              if k in model.state_dict() and v.shape == model.state_dict()[k].shape}
        model.load_state_dict(sd, strict=False)
        if "crit" in ck:
            crit.load_state_dict(ck["crit"])
        print(f"initialized weights from {args.init_from} ({len(sd)} tensors)")

    print(f"RSN v3: {model.param_count():,} params, RF {model.rf}, window {WINDOW}, "
          f"lookahead {model.lookahead} ({model.lookahead / 16:.1f} ms), {n_utt} embeddings | "
          f"batch {args.batch}, segments {args.seg_hops} hops ({args.seg_hops * 5} ms) | "
          f"{dev}{' +AMP' if amp else ''}{' +GAN' if gan else ''}")

    if args.bench:
        model.train()
        t0 = None
        for i, (x, y, a, u, v) in enumerate(dl):
            if i == args.bench:
                break
            if i == 5:
                if dev.type == "cuda":
                    torch.cuda.synchronize()
                t0 = time.time()
            x, y, a, u, v = x.to(dev), y.to(dev), a.to(dev), u.to(dev), v.to(dev)
            with torch.autocast(dev.type, enabled=amp):
                e = model(x, u).squeeze(1).squeeze(1)
            loss, _ = crit(e.float(), y, a, v)
            opt.zero_grad(set_to_none=True)
            scaler.scale(loss).backward()
            scaler.step(opt)
            scaler.update()
        if dev.type == "cuda":
            torch.cuda.synchronize()
        if t0 is None:
            raise SystemExit(f"--bench needs > 5 batches; the dataset yields {len(dl)}.")
        dt = (time.time() - t0) / max(args.bench - 5, 1)
        mem = (f", peak GPU {torch.cuda.max_memory_allocated() / 2**30:.1f} GiB"
               if dev.type == "cuda" else "")
        print(f"\nthroughput: {dt * 1000:.0f} ms/step -> 100 K steps = "
              f"{dt * 100_000 / 3600:.1f} h{mem}")
        return model, hist, ds

    out = Path(args.out) if args.out else None
    if out:
        out.mkdir(parents=True, exist_ok=True)
        (out / "meta.json").write_text(json.dumps(meta, indent=2))

    def ckpt_blob(vloss=None, vrho=None):
        b = {"model": model.state_dict(), "opt": opt.state_dict(),
             "crit": crit.state_dict(), "scaler": scaler.state_dict(),
             "step": step, "n_emb": n_utt, "hist": hist,
             "g_recent": list(g_recent), "val": vloss, "val_rho": vrho, "meta": meta,
             "arch": arch, "loss_cfg": loss_cfg, "fmt": D.FMT,
             "qat": ({k: list(v) for k, v in model.qat.items()} if model.qat else None)}
        if gan is not None:
            b["gan"] = gan.state_dict()
        return b

    if getattr(args, "qat", False):
        model.qat_stoch = bool(getattr(args, "qat_stoch", False))
        src_qat = None
        if args.init_from and Path(args.init_from).exists():
            src_qat = torch.load(args.init_from, map_location="cpu", weights_only=False).get("qat")
        if args.resume and Path(args.resume).exists():
            src_qat = torch.load(args.resume, map_location="cpu", weights_only=False).get("qat") or src_qat
        if src_qat:
            model.qat = {k: tuple(v) for k, v in src_qat.items()}
            print(f"  QAT: {len(model.qat)} tensor ranges carried over from the checkpoint")
        else:
            model.qat_begin_calibration()
            model.eval()
            with torch.no_grad():
                it_ = iter(dl)
                for _ in range(args.qat_calib):
                    xb, _yb, _ab, ub, _vb = next(it_)
                    model(xb.to(dev), ub.to(dev))
            model.train()
            rq = model.qat_end_calibration()
            w = max(rq, key=lambda k: rq[k][1] - rq[k][0])
            print(f"  QAT: {len(rq)} tensors calibrated on {args.qat_calib} batches "
                  f"(p0.01/p99.99); widest {w} {tuple(round(v, 2) for v in rq[w])}")

    if (not getattr(args, "no_out_calib", False) and step == 0
            and not args.init_from and not args.resume):
        xb0, yb0, ab0, ub0, vb0 = next(iter(dl))
        g_cal = calibrate_output(model, xb0.to(dev), ub0.to(dev),
                                 yb0.to(dev), vb0.to(dev))
        print(f"  output head calibrated: out_proj x{g_cal:.4g}  "
              f"(head sat at 1/{g_cal / CAL_RATIO:.0f} of the target's RMS, "
              f"now at the requested 1/{1.0 / CAL_RATIO:.0f})")

    t0 = time.time()
    run_loss, run_n, run_nan, run_rho = 0.0, 0, 0, float("nan")
    rogue_seen = 0
    gsum, gmax, gclip, gskip, grogue = 0.0, 0.0, 0, 0, 0
    g_recent = deque(resumed_g, maxlen=CLIP_WIN)
    for x, y, a, u, v in _forever(dl):
        x, y, a, u, v = x.to(dev), y.to(dev), a.to(dev), u.to(dev), v.to(dev)
        for g in opt.param_groups:
            g["lr"] = lr_at(step, args.lr, args.warmup, args.steps)

        with torch.autocast(dev.type, enabled=amp):
            e_pred = model(x, u).squeeze(1).squeeze(1)
        loss, parts = crit(e_pred.float(), y, a, v)
        if getattr(args, "debug_terms", 0) and step < args.debug_terms:
            tg = crit.term_grads(e_pred.float(), y, a, v)
            print(f"  [terms] step {step}: " + "  ".join(
                f"{k}: loss {v[0]:.4g} |g| {v[1]:.4g} max {v[2]:.4g}"
                for k, v in tg.items()))
        if gan is not None:
            g_loss, g_parts = gan.generator_loss(e_pred.float(), y, a, v)
            loss = loss + g_loss
            parts.update(g_parts)

        opt.zero_grad(set_to_none=True)
        scaler.scale(loss).backward()
        scaler.unscale_(opt)
        thr = clip_threshold(g_recent, args.clip)
        gn_ = float(torch.nn.utils.clip_grad_norm_(model.parameters(), thr))
        rogue = (len(g_recent) >= 16 and math.isfinite(gn_)
                 and gn_ > ROGUE_MULT * float(np.median(g_recent)))
        if math.isfinite(gn_) and not rogue:
            scaler.step(opt)
            g_recent.append(gn_)
            gsum += gn_
            gmax = max(gmax, gn_)
            gclip += int(gn_ > thr)
        elif rogue:
            grogue += 1
            gmax = max(gmax, gn_)
            if rogue_seen < ROGUE_REPORT:
                rogue_seen += 1
                with torch.no_grad():
                    ep = e_pred.detach().float()
                    yh = crit.synth(torch.where(
                        v.to(ep.dtype).repeat_interleave(dsp.HOP, 1)[:, :ep.shape[-1]] > 0,
                        ep, y), a)
                    ys = crit.synth(y, a)
                print(f"  [rogue {rogue_seen}] step {step}  |g| {gn_:.4g} vs median "
                      f"{float(np.median(g_recent)):.4g}  parts "
                      f"{ {k: round(vv, 4) for k, vv in parts.items()} }")
                print(f"            e_pred |max| {float(ep.abs().max()):.4g}  "
                      f"y |max| {float(y.abs().max()):.4g}  "
                      f"synth(e_pred) |max| {float(yh.abs().max()):.4g}  "
                      f"synth(y) |max| {float(ys.abs().max()):.4g}")
                print(f"            a |max| {float(a.abs().max()):.4g}  "
                      f"ref_rms {float(crit.ref_rms):.4g}  ref_abs {float(crit.ref_abs):.4g}  "
                      f"fallback {dsp.LpcSynthExact.fallback_rate() * 100:.3f}%  "
                      f"valid {int(v.sum())}/{v.numel()}")
                tg = crit.term_grads(ep, y, a, v)
                print("            terms: " + "  ".join(
                    f"{k} loss {vv[0]:.4g} |g| {vv[1]:.4g}" for k, vv in tg.items()))
        else:
            gskip += 1
            if getattr(args, "debug_nan", 0) and gskip <= args.debug_nan:
                dsp.LpcSynthExact.debug = True
                with torch.no_grad():
                    try:
                        crit(e_pred.detach().float(), y, a, v)
                    except Exception as _e:
                        print(f"    [synth] re-run raised {type(_e).__name__}: {_e}")
                dsp.LpcSynthExact.debug = False
                if getattr(args, "dump_nan", None):
                    import numpy as _np
                    _np.savez_compressed(
                        f"{args.dump_nan}_step{step}.npz",
                        x=x.detach().cpu().numpy(), y=y.detach().cpu().numpy(),
                        a=a.detach().cpu().numpy(), v=v.detach().cpu().numpy(),
                        u=u.detach().cpu().numpy(),
                        e_pred=e_pred.detach().float().cpu().numpy())
                    print(f"    [dump] {args.dump_nan}_step{step}.npz")
                with torch.no_grad():
                    ep = e_pred.detach().float()
                    fin = torch.isfinite(ep)
                    bad_items = (~fin).any(dim=-1).nonzero().flatten().tolist()
                    gbad = [n for n, prm in model.named_parameters()
                            if prm.grad is not None
                            and not torch.isfinite(prm.grad).all()]
                    print(f"  [nan {gskip}] step {step}  loss "
                          f"{float(loss) if math.isfinite(float(loss)) else 'NON-FINITE'}"
                          f"  parts { {k: round(v, 4) for k, v in parts.items()} }")
                    print(f"           e_pred: finite={bool(fin.all())} "
                          f"|max|={float(ep[fin].abs().max()) if fin.any() else float('nan'):.4g} "
                          f"bad items {bad_items}")
                    print(f"           y |max|={float(y.abs().max()):.4g}  "
                          f"a |max|={float(a.abs().max()):.4g}  "
                          f"valid {int(v.sum())}/{v.numel()}  "
                          f"scaler {float(scaler.get_scale()) if amp else 1.0:.3g}")
                    print(f"           non-finite grads in {len(gbad)} tensors"
                          + (f", first: {gbad[0]}" if gbad else ""))
            if gskip > args.max_skip:
                raise SystemExit(f"\n{gskip} non-finite gradients within one log window "
                                 f"(step {step}): the loss is diverging.")
        scaler.update()
        if gan is not None:
            gan.discriminator_step(e_pred.detach().float(), y, a, v)
        step += 1
        fl = float(loss)
        if math.isfinite(fl):
            run_loss += fl
            run_n += 1
        else:
            run_nan += 1
        if step % args.log_every == 0:
            with torch.no_grad():
                run_rho = rho_valid(e_pred.detach().float(), y, v)

        if step % args.log_every == 0:
            el = time.time() - t0
            eta = el / max(step, 1) * (args.steps - step) / 3600
            extra = "".join(f" {k} {parts[k]:.3f}" for k in ("dc", "adv", "fm", "d") if k in parts)
            nsteps = max(run_n + run_nan, 1)
            mean_loss = run_loss / max(run_n, 1)
            print(f"  step {step:6d}/{args.steps}  loss {mean_loss:.4f}"
                  f"  (L1 {parts['l1']:.3f} wave {parts['wave']:.3f} MRSTFT {parts['mrstft']:.3f}{extra})"
                  f"  rho {run_rho:+.3f}"
                  f"  lr {lr_at(step, args.lr, args.warmup, args.steps):.2e}"
                  f"  |g| {gsum / max(run_n, 1):7.2f} max {gmax:8.2f}"
                  f" clip {100.0 * gclip / nsteps:3.0f}%@{clip_threshold(g_recent, args.clip):.0f}"
                  + (f" SKIP {gskip}" if gskip else "")
                  + (f" ROGUE {grogue}" if grogue else "")
                  + (f" NAN {run_nan}" if run_nan else "")
                  + f"  ETA {eta:.1f} h")
            hist.append(mean_loss)
            run_loss, run_n, run_nan = 0.0, 0, 0
            gsum, gmax, gclip, gskip, grogue = 0.0, 0.0, 0, 0, 0

        if out and step % args.ckpt_every == 0:
            vloss, vrho = evaluate(model, crit, vdl, dev, amp)
            torch.save(ckpt_blob(vloss, vrho), out / f"rsn_{step // 1000:03d}k.pt")
            torch.save(ckpt_blob(vloss, vrho), out / "rsn_last.pt")
            print(f"  [ckpt] step {step}  val {vloss:.4f}  val rho {vrho:.3f}  -> "
                  f"{out}/rsn_{step // 1000:03d}k.pt")

        if step >= args.steps:
            break

    if out:
        vloss, vrho = evaluate(model, crit, vdl, dev, amp)
        torch.save(ckpt_blob(vloss, vrho), out / "rsn.pt")
        torch.save(ckpt_blob(vloss, vrho), out / "rsn_last.pt")
        print(f"-> {out}/rsn.pt  val {vloss:.4f}  val rho {vrho:.3f}  ({time.time() - t0:.0f}s)")
    return model, hist, ds


def run_epochs(args):
    assert_rf()
    dev = torch.device(args.device)
    torch.manual_seed(0)
    items, _, _ = load_dataset(args)
    n_utt = max(int(it[3]) for it in items) + 1
    ds = D.SegmentDataset(items, seg_hops=args.seg_hops)
    dl = torch.utils.data.DataLoader(ds, batch_size=args.batch, shuffle=True,
                                     collate_fn=D.collate)
    model, crit, _arch, _cfg = build_model_and_loss(args, n_utt, dev)
    if not getattr(args, "no_out_calib", False):
        xb0, yb0, _ab0, ub0, vb0 = next(iter(dl))
        g_cal = calibrate_output(model, xb0.to(dev), ub0.to(dev),
                                 yb0.to(dev), vb0.to(dev))
        print(f"  output head calibrated: out_proj x{g_cal:.4g}  "
              f"(head sat at 1/{g_cal / CAL_RATIO:.0f} of the target's RMS, "
              f"now at the requested 1/{1.0 / CAL_RATIO:.0f})")
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    print(f"RSN v3: {model.param_count():,} params, lookahead {model.lookahead}, "
          f"{len(items)} items / {n_utt} embeddings, batch {args.batch}, "
          f"segments {args.seg_hops} hops")
    step, hist, rhos = 0, [], []
    g_recent = deque(maxlen=CLIP_WIN)
    for epoch in range(args.epochs):
        tot, l1s, mss, nb, gtot, gpk, rh = 0.0, 0.0, 0.0, 0, 0.0, 0.0, []
        for x, y, a, u, v in dl:
            x, y, a, u, v = x.to(dev), y.to(dev), a.to(dev), u.to(dev), v.to(dev)
            for g in opt.param_groups:
                g["lr"] = lr_at(step, args.lr, args.warmup, max(args.steps, 1))
            e = model(x, u).squeeze(1).squeeze(1)
            loss, parts = crit(e, y, a, v)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            gn = float(torch.nn.utils.clip_grad_norm_(
                model.parameters(), clip_threshold(g_recent, getattr(args, "clip", 0.0))))
            if math.isfinite(gn):
                g_recent.append(gn)
                opt.step()
                gtot += gn
                gpk = max(gpk, gn)
            step += 1
            tot += float(loss); l1s += parts["l1"]; mss += parts["mrstft"]; nb += 1
            rh.append(rho_valid(e.detach(), y, v))
        hist.append(tot / max(nb, 1))
        rhos.append(float(np.median(rh)))
        print(f"  epoch {epoch + 1:3d}  loss {hist[-1]:.4f}  (L1 {l1s / max(nb, 1):.3f}  "
              f"MRSTFT {mss / max(nb, 1):.3f})  rho {rhos[-1]:.3f}  |g| {gtot / max(nb, 1):.2f} "
              f"max {gpk:.2f}  lr {lr_at(step, args.lr, args.warmup, max(args.steps, 1)):.2e}")
    return model, hist, ds, rhos


def add_common_args(ap):
    ap.add_argument("--cache", default=None, help="prep.py shard directory")
    ap.add_argument("--pairs", type=int, default=8)
    ap.add_argument("--dur", type=float, default=2.0)
    ap.add_argument("--seg_hops", type=int, default=300)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--steps", type=int, default=100_000)
    ap.add_argument("--epochs", type=int, default=0)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--warmup", type=int, default=1000)
    ap.add_argument("--ckpt_every", type=int, default=10_000)
    ap.add_argument("--log_every", type=int, default=200)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--clip", type=float, default=0.0, help="0 = adaptive (4x running median)")
    ap.add_argument("--max_skip", type=int, default=50)
    ap.add_argument("--qat", action="store_true",
                    help="quantization-aware fine-tuning: train through the INT8 fake-quant "
                         "of every tensor the exported QDQ graph quantizes (model._forward_qat)")
    ap.add_argument("--qat-stoch", dest="qat_stoch", action="store_true",
                    help="QAT with stochastic rounding: robustness to the +-1 LSB rounding "
                         "differences between the simulation, onnxruntime and the NPU")
    ap.add_argument("--qat-calib", dest="qat_calib", type=int, default=50,
                    help="batches used to calibrate the QAT ranges")
    ap.add_argument("--emb", choices=("file", "singer", "none"), default="file",
                    help="one embedding per recording (cache default) or per SINGER "
                         "(what rsn.calibrate fits for a new voice); needs utt_map.json")
    ap.add_argument("--holdout", nargs="+", default=None,
                    help="singers to leave out of training and validation, "
                         "for testing calibration on unseen singers")
    ap.add_argument("--exclude", default=None,
                    help="rsn.screen JSON: items whose filters diverge in the loss-path "
                         "synthesis (applied after the train/val split)")
    ap.add_argument("--no-out-calib", dest="no_out_calib", action="store_true",
                    help="skip the data-driven output-head calibration and use "
                         "OUT_GAIN as-is (v2 behaviour).")
    ap.add_argument("--debug-terms", dest="debug_terms", type=int, default=0,
                    help="for the first N steps, report the gradient norm of "
                         "EACH loss term separately. The aggregate |g| hit 2e8 "
                         "while MRSTFT contributed 0.000, so the magnitude has "
                         "to be attributed before it can be fixed.")
    ap.add_argument("--synth-fp64", dest="synth_fp64", action="store_true",
                    help="run the loss-path synthesis in float64. The forward "
                         "scan peaked at 3.391e+38 against float32's "
                         "3.4028e+38 -- 0.3%% of margin -- and the backward "
                         "has the same dynamic range with no guard.")
    ap.add_argument("--dump-nan", dest="dump_nan", default=None,
                    help="path PREFIX; save the offending batch as an .npz so "
                         "the failing frame can be reproduced offline.")
    ap.add_argument("--debug-nan", dest="debug_nan", type=int, default=0,
                    help="on the first N non-finite gradients, print where it "
                         "came from: the model output, the loss parts, the "
                         "GradScaler scale, and which parameter tensors hold a "
                         "non-finite grad. rsn.diag_nan already clears the data, "
                         "taps, synthesis, loss and backward on a zero "
                         "prediction, so what is left needs the model.")
    ap.add_argument("--lookahead", type=int, default=0,
                    help="samples of lookahead @16 kHz (0 = causal, 126 = symmetric). "
                         "Costs L/16 ms of latency and D_PATH += 3L; no NPU cost.")
    ap.add_argument("--augment-db", dest="augment_db", type=float, nargs=2, default=[-12.0, 3.0],
                    help="random gain range (dB) on ch1 + target; pass 0 0 to disable")
    ap.add_argument("--synth-interp", dest="synth_interp", choices=("firmware", "hop"),
                    default="firmware")
    ap.add_argument("--w-l1", dest="w_l1", type=float, default=1.0)
    ap.add_argument("--l1-mode", dest="l1_mode", default="plain", choices=("plain", "energy", "l2"))
    ap.add_argument("--w-wave", dest="w_wave", type=float, default=1.0,
                    help="L1 between the SYNTHESIZED waveforms / reference scale: the "
                         "phase-aware term (its gradient is ~400x the residual L1's)")
    ap.add_argument("--w-mrstft", dest="w_mrstft", type=float, default=0.5)
    ap.add_argument("--w-dc", dest="w_dc", type=float, default=0.0)
    ap.add_argument("--mrstft-16k", dest="mrstft_16k", action="store_true")
    ap.add_argument("--probe-width", dest="probe_width", type=int, default=0)
    ap.add_argument("--probe-dilations", dest="probe_dilations", nargs="+", type=int, default=None)
    ap.add_argument("--gan", action="store_true", help="stage 2: adversarial fine-tune (rsn.gan)")
    ap.add_argument("--w-adv", dest="w_adv", type=float, default=0.05)
    ap.add_argument("--w-fm", dest="w_fm", type=float, default=1.0)
    ap.add_argument("--lr-d", dest="lr_d", type=float, default=2e-4)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    ap.add_argument("--amp", action="store_true")
    ap.add_argument("--out", default=None)
    ap.add_argument("--resume", default=None)
    ap.add_argument("--init-from", dest="init_from", default=None,
                    help="load weights only (warm start across lookahead / into --gan)")
    ap.add_argument("--bench", type=int, default=0)
    ap.add_argument("--smoke", action="store_true")


def main():
    ap = argparse.ArgumentParser()
    add_common_args(ap)
    args = ap.parse_args()
    if args.augment_db and args.augment_db[0] == 0 and args.augment_db[1] == 0:
        args.augment_db = None
    if args.smoke:
        args.pairs, args.dur, args.seg_hops = 8, 2.0, 200
        args.batch, args.epochs, args.steps, args.warmup = 4, 5, 0, 20
        args.lr, args.device, args.workers = 1e-3, "cpu", 0
        args.clip = 0.0
        run_epochs(args)
        return
    run(args)


if __name__ == "__main__":
    main()

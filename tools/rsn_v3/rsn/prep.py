"""Build the training cache from a corpus of singing.

    python -m rsn.prep --corpus <dir> --out <cache> --swiftf0 <swiftf0_float.onnx>

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import argparse
import json
import os
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import soundfile as sf

from . import data as D
from . import dsp
from . import f0logic
from . import psola

AUDIO_EXT = (".wav", ".flac", ".mp3", ".m4a")


def _load16(path: str, max_secs: float = 0.0) -> np.ndarray:
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(1)
    x = D.resample_16k(x, sr)
    if max_secs and len(x) > int(max_secs * dsp.FS16):
        x = x[: int(max_secs * dsp.FS16)]
    p = float(np.max(np.abs(x)))
    return x / p * 0.5 if p > 1e-6 else x


def _one_file(job):
    (path, n_items, utt, singer, onnx, res_scale, max_secs, seed, shift_range) = job
    out = []
    try:
        xs = _load16(path, max_secs)
        if len(xs) < 4 * dsp.FS16 // 10:
            return out, 0, 0
        f0s, vs = D.f0_swiftf0(xs, onnx)
        src_side = D.analyse_side(xs)
        a_s, e_s, nh = src_side
        nh = min(nh, len(f0s))
        if vs[:nh].mean() < 0.3:
            return out, 0, int(nh)
        gci, valid = psola.detect_gci(e_s[: nh * dsp.HOP], f0s[:nh], vs[:nh])
        n_v, n_ok = int(vs[:nh].sum()), int(valid.sum())
        if n_ok < 0.3 * nh:
            return out, n_ok, n_v - n_ok
    except Exception as e:
        print(f"    skip {os.path.basename(path)}: {e}")
        return out, 0, 0

    rng = np.random.default_rng(seed)
    for i in range(n_items):
        try:
            shift, mode, vib, scale, root = f0logic.random_config(rng, shift_range)
            f0c = f0logic.f0_corr_contour(f0s[:nh], vs[:nh], shift_st=shift, mode=mode,
                                          vib=vib, scale=scale, root=root)
            it = D.extract(xs, f0s[:nh], vs[:nh], f0c, utt=utt, src_side=src_side,
                           gci_valid=(gci, valid), res_scale=res_scale,
                           cfg=D.cfg_encode(shift, mode, vib, scale, root))
            out.append(it)
        except Exception as e:
            print(f"    skip {os.path.basename(path)} (item {i}): {e}")
    return out, n_ok, n_v - n_ok


def measure_res_scale(files, n=24, seed=0, max_secs=0.0):
    rng = np.random.default_rng(seed)
    pick = rng.choice(len(files), size=min(n, len(files)), replace=False)
    peaks = []
    for i in pick:
        x = _load16(files[i], max_secs)
        if len(x) < dsp.WIN + dsp.HOP:
            continue
        a, _, _ = dsp.lpc_per_hop(x)
        e = dsp.residual_from_lpc(x, a)
        peaks.append(np.percentile(np.abs(e), 99.9))
    p999 = float(np.median(peaks))
    scale = 1.0 / max(p999, 1e-9)
    print(f"  residual 99.9th pct = {p999:.5f} over {len(peaks)} files -> RES_SCALE = {scale:.1f}")
    return scale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--swiftf0", required=True)
    ap.add_argument("--items-per-file", type=int, default=3,
                    help="F0_corr configurations per recording (random shift / "
                         "snap mode / scale; see f0logic.random_config). The "
                         "source analysis is shared, so extra items are cheap "
                         "to make but cost RAM at train time (~335 KB/s each).")
    ap.add_argument("--shift-range", type=float, nargs=2, default=[-4.0, 4.0],
                    help="semitone range of the transposition")
    ap.add_argument("--gain", choices=("energy", "verbatim", "envelope"), default="energy",
                    help="level convention of the PSOLA target (rsn.psola)")
    ap.add_argument("--max-files", type=int, default=0,
                    help="cap the corpus at N files, dealt ROUND-ROBIN over singers")
    ap.add_argument("--max-secs", type=float, default=6.0,
                    help="cap each recording (0 = whole file)")
    ap.add_argument("--exclude", nargs="*", default=[],
                    help="drop files whose path contains any of these (case-"
                         "insensitive). VocalSet: --exclude fry inhaled spoken lip_trill")
    ap.add_argument("--singer-depth", type=int, default=1)
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--shard-size", type=int, default=64)
    ap.add_argument("--val-frac", type=float, default=0.05)
    ap.add_argument("--res-scale", type=float, default=None)
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()

    files = sorted(str(p) for p in Path(a.corpus).rglob("*")
                   if p.suffix.lower() in AUDIO_EXT)
    n_all = len(files)
    if a.exclude:
        pats = [e.lower() for e in a.exclude]
        files = [f for f in files if not any(pt in f.lower() for pt in pats)]
    n_excl = n_all - len(files)

    def singer_key(f):
        rel = Path(f).relative_to(a.corpus).parts
        d = min(a.singer_depth, max(len(rel) - 1, 0))
        return "/".join(rel[:d]) if d else "single"

    n_cap = 0
    if a.max_files and a.max_files < len(files):
        by_s = {}
        for f in files:
            by_s.setdefault(singer_key(f), []).append(f)
        rr = np.random.default_rng(0)
        lists = []
        for k in sorted(by_s):
            v = by_s[k][:]
            rr.shuffle(v)
            lists.append(v)
        order = []
        for i in range(max(len(v) for v in lists)):
            order += [v[i] for v in lists if i < len(v)]
        n_cap = len(files) - a.max_files
        files = sorted(order[:a.max_files])
    if not files:
        raise SystemExit(f"no audio under {a.corpus}")

    singers = {}
    for f in files:
        singers.setdefault(singer_key(f), len(singers))
    drops = ([f"{n_excl} dropped by --exclude"] if a.exclude else [])
    drops += ([f"{n_cap} dropped by --max-files"] if n_cap else [])
    print(f"corpus: {len(files)} of {n_all} files" + (f" ({'; '.join(drops)})" if drops else "")
          + f", {len(singers)} singers, {a.items_per_file} items/file, shifts {a.shift_range} st")
    if len(singers) < 5 and len(files) > 50:
        print(f"  WARNING: {len(files)} files resolved to only {len(singers)} singer id(s). "
              f"Check --corpus / --singer-depth (VocalSet wants .../VocalSet/FULL, depth 1).")

    res_scale = a.res_scale or measure_res_scale(files, max_secs=a.max_secs)
    jobs = [(f, a.items_per_file, i, singers[singer_key(f)], a.swiftf0, res_scale,
             a.max_secs, a.seed * 100003 + i, tuple(a.shift_range))
            for i, f in enumerate(files)]
    secs = a.max_secs if a.max_secs else 8.0
    est_gb = len(jobs) * a.items_per_file * secs * 335e3 / 1e9
    print(f"jobs: {len(jobs)} files x {a.items_per_file} items, {a.workers} workers; "
          f"~{est_gb:.1f} GB resident at train time (trim --items-per-file / --max-secs first)")

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    stale = sorted(out.glob("shard_*.npz"))
    if stale:
        print(f"  removing {len(stale)} shard(s) from a previous prep in {out}")
        for f in stale:
            f.unlink()
    (out / "meta.json").unlink(missing_ok=True)
    shard, n_items, n_shard = [], 0, 0
    ok_hops = bad_hops = 0
    with ProcessPoolExecutor(max_workers=a.workers) as ex:
        for k, (got, nok, nbad) in enumerate(ex.map(_one_file, jobs, chunksize=1)):
            ok_hops += nok
            bad_hops += nbad
            for item in got:
                shard.append(item)
                n_items += 1
                if len(shard) >= a.shard_size:
                    D.save_items(shard, out / f"shard_{n_shard:04d}.npz")
                    n_shard += 1
                    shard = []
            if (k + 1) % 25 == 0:
                tot = ok_hops + bad_hops
                print(f"  {k + 1}/{len(jobs)} files, {n_items} items kept"
                      + (f", GCI detection kept {100.0 * ok_hops / tot:.0f}% of voiced hops"
                         if tot else ""))
    if shard:
        D.save_items(shard, out / f"shard_{n_shard:04d}.npz")
        n_shard += 1

    meta = {
        "fmt": D.FMT,
        "target": "lp-psola",
        "gain": a.gain,
        "res_scale": res_scale,
        "n_items": n_items,
        "n_shards": n_shard,
        "n_utterances": len(jobs),
        "utt_is_file": True,
        "n_singers": len(singers),
        "items_per_file": a.items_per_file,
        "shift_range": list(a.shift_range),
        "max_files": a.max_files,
        "max_secs": a.max_secs,
        "exclude": a.exclude,
        "gci_kept_hops": ok_hops,
        "gci_rejected_hops": bad_hops,
        "val_frac": a.val_frac,
        "window": 352,
        "hop": dsp.HOP,
    }
    (out / "meta.json").write_text(json.dumps(meta, indent=2))
    (out / "utt_map.json").write_text(json.dumps({
        "root": str(a.corpus),
        "utt_singer": {str(j[2]): singer_key(j[0]) for j in jobs},
        "utt_file": {str(j[2]): j[0] for j in jobs},
    }, indent=1))
    print(f"\n-> {out}: {n_items} items in {n_shard} shards")
    print(f"   RES_SCALE = {res_scale:.1f}  (the firmware must use this value)")


if __name__ == "__main__":
    main()

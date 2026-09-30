"""Training items (input channels, targets, LPC) from singing recordings, the
pitch tracker, and a synthetic source for tests.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import numpy as np
import torch
from scipy.signal import lfilter

from . import dsp
from . import f0logic
from . import psola

FMT = "v3-psola"
MODES = ("track", "snap")
VIBS = ("ratio", "hold")


def analyse_side(x16: np.ndarray):
    a, _, nh = dsp.lpc_per_hop(x16)
    return a, dsp.residual_from_lpc(x16, a), nh


def extract(x16, f0_src, voiced, f0_corr, utt=0, src_side=None, gci_valid=None,
            res_scale=dsp.RES_SCALE, cfg=None):
    a_s, e_full, nh_s = src_side if src_side is not None else analyse_side(x16)
    nh = min(nh_s, len(f0_src), len(f0_corr), len(voiced))
    a_s = a_s[:nh]
    e_s = e_full[: nh * dsp.HOP]
    g = dsp.residual_sanity(x16, e_s)
    if g < 3.0:
        raise ValueError(f"residual prediction gain {g:.1f} dB (< 3): the LPC "
                         "analysis is not whitening -- check the a[] sign convention.")
    f0_s = np.asarray(f0_src[:nh], dtype=np.float64)
    f0_c = np.asarray(f0_corr[:nh], dtype=np.float64)
    v = np.asarray(voiced[:nh], dtype=bool)
    if gci_valid is None:
        gci, valid = psola.detect_gci(e_s, f0_s, v)
    else:
        gci, valid = gci_valid
        gci = gci[gci < nh * dsp.HOP]
        valid = np.asarray(valid, bool)[:nh]
    exc, wraps = dsp.excitation_with_wraps(np.maximum(f0_c, 1e-3), voiced=v)
    ch2 = np.repeat(dsp.f0_norm(f0_s), dsp.HOP)
    ch3 = np.repeat(dsp.f0_norm(f0_c), dsp.HOP)
    x = np.stack([exc, e_s * res_scale, ch2, ch3]).astype(np.float32)
    a_out = a_s.astype(np.float32)
    for nm, arr in (("input", x), ("a", a_out)):
        if not np.isfinite(arr).all():
            raise ValueError(f"{nm} contains non-finite values")
    cfg = np.asarray(cfg if cfg is not None else [0.0, 0, 0, 0, 0], dtype=np.float32)
    return (x, a_out, int(utt), valid.astype(bool), gci.astype(np.int64),
            wraps.astype(np.int64), cfg)


def materialize(item, lookahead: int = 0, gain: str = "energy",
                fill: str = "psola"):
    x, a, utt, valid, gci, wraps, _cfg = item
    f0s = 440.0 * 2.0 ** (x[2][::dsp.HOP].astype(np.float64) * 3.0)
    f0c = 440.0 * 2.0 ** (x[3][::dsp.HOP].astype(np.float64) * 3.0)
    y, v2 = psola.psola_target(x[1], gci, wraps, f0s, f0c, valid,
                               lookahead=int(lookahead), gain=gain, fill=fill)
    return (x, y.astype(np.float32), a, int(utt), v2.astype(bool))


def cfg_encode(shift, mode, vib, scale, root):
    return np.array([shift, MODES.index(mode), VIBS.index(vib),
                     0 if scale is None else 1, root], dtype=np.float32)


def cfg_decode(cfg):
    c = np.asarray(cfg, dtype=np.float64)
    return {"shift_st": float(c[0]), "mode": MODES[int(c[1])], "vib": VIBS[int(c[2])],
            "scale": "major" if int(c[3]) else "chromatic", "root": int(c[4])}


def synth_source(rng, dur_s=2.0, f0_src=None, vib_cents=None):
    fs = dsp.FS16
    n = int(dur_s * fs)
    t = np.arange(n) / fs
    f0_src = f0_src if f0_src is not None else rng.uniform(110, 260)
    depth = rng.uniform(10, 40) if vib_cents is None else float(vib_cents)
    vib = 2 ** (depth / 1200 * np.sin(2 * np.pi * rng.uniform(4.5, 6.5) * t))
    formants = [(rng.uniform(500, 800), 60), (rng.uniform(1100, 1600), 90),
                (rng.uniform(2400, 2900), 120), (rng.uniform(3300, 3800), 150)]
    f0 = f0_src * vib
    ph = 2 * np.pi * np.cumsum(f0) / fs
    src = np.zeros(n)
    for m in range(1, int(fs / 2 / f0_src)):
        src += np.sin(m * ph + rng.uniform(0, 2 * np.pi)) / m ** 1.4
    src += 0.02 * rng.standard_normal(n)
    y = src
    for fc, bw in formants:
        r = np.exp(-np.pi * bw / fs)
        y = lfilter([1.0], [1.0, -2 * r * np.cos(2 * np.pi * fc / fs), r * r], y)
    y = (y / (np.max(np.abs(y)) + 1e-9) * 0.2).astype(np.float64)
    nh = (n - dsp.WIN) // dsp.HOP + 1
    hop_t = (np.arange(nh) * dsp.HOP + dsp.WIN - dsp.HOP / 2) / fs
    return y, f0_src * np.interp(hop_t, t, vib)


def synthetic_res_scale(x16: np.ndarray) -> float:
    a, _, _ = dsp.lpc_per_hop(x16)
    e = dsp.residual_from_lpc(x16, a)
    return float(1.0 / max(np.percentile(np.abs(e), 99.9), 1e-9))


def synthetic_item(rng, dur_s=2.0, f0_src=None, shift_st=0.0, utt=0,
                   vib_cents=None, mode="track", lookahead=0, gain="energy",
                   materialized=True, res_scale=None):
    x16, f0 = synth_source(rng, dur_s, f0_src, vib_cents)
    voiced = np.ones(len(f0), bool)
    f0c = f0logic.f0_corr_contour(f0, voiced, shift_st=shift_st, mode=mode)
    rs = synthetic_res_scale(x16) if res_scale is None else float(res_scale)
    it = extract(x16, f0, voiced, f0c, utt=utt, res_scale=rs,
                 cfg=cfg_encode(shift_st, mode, "ratio", None, 0))
    return materialize(it, lookahead, gain) if materialized else it


def build_synthetic(n_pairs=8, seed=0, dur_s=2.0, lookahead=0, gain="energy",
                    return_scale=False):
    rng = np.random.default_rng(seed)
    srcs = []
    for i in range(n_pairs):
        x16, f0 = synth_source(rng, dur_s)
        srcs.append((x16, f0, float(rng.uniform(-4, 4))))
    rs = float(np.median([synthetic_res_scale(x) for x, _, _ in srcs]))
    out = []
    for i, (x16, f0, s) in enumerate(srcs):
        voiced = np.ones(len(f0), bool)
        f0c = f0logic.f0_corr_contour(f0, voiced, shift_st=s, mode="track")
        it = extract(x16, f0, voiced, f0c, utt=i, res_scale=rs,
                     cfg=cfg_encode(s, "track", "ratio", None, 0))
        out.append(materialize(it, lookahead, gain))
    return (out, rs) if return_scale else out


def resample_16k(x: np.ndarray, sr: int) -> np.ndarray:
    from math import gcd
    from scipy.signal import resample_poly
    x = np.asarray(x, dtype=np.float64)
    if sr == dsp.FS16:
        return x
    g = gcd(int(sr), dsp.FS16)
    return resample_poly(x, dsp.FS16 // g, int(sr) // g)


_SWIFTF0_API = None
_SWIFTF0_SESSIONS = {}


def _swiftf0_api():
    global _SWIFTF0_API
    if _SWIFTF0_API is None:
        import sys
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "swiftf0")
        if p not in sys.path:
            sys.path.insert(0, p)
        from swiftf0.frontend import features, NFFT
        from swiftf0.decode import decode
        _SWIFTF0_API = (features, NFFT, decode)
    return _SWIFTF0_API


def _swiftf0_session(onnx_path: str):
    key = os.path.abspath(onnx_path)
    if key not in _SWIFTF0_SESSIONS:
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.intra_op_num_threads = 1
        sess = ort.InferenceSession(key, so)
        outs = [o.name for o in sess.get_outputs()]
        _SWIFTF0_SESSIONS[key] = (sess, sess.get_inputs()[0].name,
                                  outs.index("pitch_logits"), outs.index("voice_logit"))
    return _SWIFTF0_SESSIONS[key]


def f0_swiftf0(x16: np.ndarray, onnx_path: str, v_thresh: float = 0.5):
    features, NFFT, decode = _swiftf0_api()
    sess, name, i_pitch, i_voice = _swiftf0_session(onnx_path)
    xi = np.clip(np.rint(x16 * 32768.0), -32768, 32767).astype(np.int16)
    nh = (len(x16) - dsp.WIN) // dsp.HOP + 1
    f0 = np.zeros(nh)
    voiced = np.zeros(nh, dtype=bool)
    for h in range(nh):
        end = h * dsp.HOP + dsp.WIN
        if end < NFFT:
            continue
        feat = features(xi[end - NFFT: end]).astype(np.float32)
        out = sess.run(None, {name: feat.reshape(1, 1, 1, -1)})
        f0[h] = decode(out[i_pitch].reshape(-1))[0]
        voiced[h] = 1.0 / (1.0 + np.exp(-float(out[i_voice].reshape(-1)[0]))) > v_thresh
    last = 0.0
    for h in range(nh):
        if voiced[h] and f0[h] > 0:
            last = f0[h]
        elif last > 0:
            f0[h] = last
    first = next((v for v in f0 if v > 0), 0.0)
    f0[f0 <= 0] = first
    return f0, voiced


class SegmentDataset(torch.utils.data.Dataset):

    def __init__(self, items, seg_hops=300, augment_db=None):
        self.items = items
        self.seg_hops = seg_hops
        self.aug = augment_db
        self._cum = [np.concatenate(([0], np.cumsum(np.asarray(it[4], np.int64))))
                     for it in items]

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        x, y, a, utt, valid = self.items[i]
        nh = a.shape[0]
        seg = min(self.seg_hops, nh)
        if nh <= seg:
            s = 0
        else:
            c = self._cum[i]
            n_start = nh - seg + 1
            frac = (c[seg:seg + n_start] - c[:n_start]) / float(seg)
            good = np.flatnonzero(frac >= 0.8)
            pool = good if len(good) else np.arange(n_start)
            s = int(pool[int(torch.randint(len(pool), (1,)))])
        n0, n1 = s * dsp.HOP, (s + seg) * dsp.HOP
        xs = x[:, n0:n1]
        ys = y[n0:n1]
        if self.aug is not None:
            g = 10.0 ** (float(torch.empty(1).uniform_(*self.aug)) / 20.0)
            xs = xs.copy()
            xs[1] *= g
            ys = ys * g
        return (torch.from_numpy(np.ascontiguousarray(xs)).unsqueeze(1),
                torch.from_numpy(np.ascontiguousarray(ys)),
                torch.from_numpy(a[s: s + seg]),
                int(utt),
                torch.from_numpy(valid[s: s + seg].astype(np.bool_)))


def collate(batch):
    T = min(b[0].shape[-1] for b in batch)
    H = T // dsp.HOP
    x = torch.stack([b[0][..., :T] for b in batch])
    y = torch.stack([b[1][:T] for b in batch])
    a = torch.stack([b[2][:H] for b in batch])
    u = torch.tensor([b[3] for b in batch], dtype=torch.long)
    v = torch.stack([b[4][:H] for b in batch])
    return x, y, a, u, v


def save_items(items, path):
    d = {"n": np.array(len(items)), "fmt": np.array(FMT)}
    for i, it in enumerate(items):
        x, a, utt, valid, gci, wraps, cfg = it
        d[f"x_{i}"] = np.asarray(x, np.float32)
        d[f"a_{i}"] = np.asarray(a, np.float32)
        d[f"utt_{i}"] = np.array(int(utt))
        d[f"valid_{i}"] = np.asarray(valid, bool)
        d[f"gci_{i}"] = np.asarray(gci, np.int64)
        d[f"wraps_{i}"] = np.asarray(wraps, np.int64)
        d[f"cfg_{i}"] = np.asarray(cfg, np.float32)
    np.savez_compressed(path, **d)


def load_items(path, lookahead=None, gain="energy"):
    with np.load(path) as z:
        if "fmt" not in z.files or str(z["fmt"]) != FMT:
            raise SystemExit(
                f"\n{path} is not a v3 cache (WORLD-target caches carry a "
                f"target that is not a function of the inputs -- see rsn/psola.py). "
                f"Re-run rsn.prep into a NEW directory.")
        n = int(z["n"])
        raw = [(z[f"x_{i}"], z[f"a_{i}"], int(z[f"utt_{i}"]), z[f"valid_{i}"],
                z[f"gci_{i}"], z[f"wraps_{i}"], z[f"cfg_{i}"]) for i in range(n)]
    if lookahead is None:
        return raw
    return [materialize(it, lookahead, gain) for it in raw]


def utt_singers(cache):
    um = json.loads((Path(cache) / "utt_map.json").read_text())
    return {int(k): v for k, v in um["utt_singer"].items()}


def emb_mapper(meta: dict, cache=None):
    mode = (meta or {}).get("emb_mode", "file")
    hold = set((meta or {}).get("holdout") or [])
    u2s = utt_singers(cache) if (mode == "singer" or hold) and cache else {}
    if mode == "singer":
        sid = {n: i for i, n in enumerate(meta.get("singers") or [])}
        return (lambda u: sid[u2s[int(u)]]), hold, u2s
    if mode == "none":
        return (lambda u: 0), hold, u2s
    return (lambda u: int(u)), hold, u2s


def _load_shard(job):
    sh, lookahead, gain = job
    return load_items(sh, lookahead=lookahead, gain=gain)


def load_cache(cdir, lookahead, gain=None, workers=None):
    cdir = Path(cdir)
    meta = json.loads((cdir / "meta.json").read_text())
    if meta.get("fmt") != FMT:
        raise SystemExit(f"{cdir}/meta.json is not a v3 cache (fmt={meta.get('fmt')})")
    gain = gain or meta.get("gain", "energy")
    shards = sorted(cdir.glob("shard_*.npz"))
    if workers is None:
        import os
        workers = min(len(shards), max(1, (os.cpu_count() or 1) // 4), 48)
    if workers > 1 and len(shards) > 1:
        import multiprocessing as mp
        with mp.get_context("fork").Pool(workers) as pool:
            parts = pool.map(_load_shard, [(sh, lookahead, gain) for sh in shards])
        items = [it for part in parts for it in part]
    else:
        items = []
        for sh in shards:
            items += load_items(sh, lookahead=lookahead, gain=gain)
    return items, meta

"""Export a checkpoint to float and INT8 ONNX for the NPU.

    python -m rsn.export --ckpt <ckpt> --out <prefix> --calib-cache <cache>

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

from . import data as D
from .model import CH, CIN, K, WINDOW, RSNv2, assert_rf, from_arch, output_slice


class RSNDeploy(nn.Module):

    def __init__(self, src: RSNv2):
        super().__init__()
        self.in_proj = src.in_proj
        self.convs = src.convs
        self.out_proj = src.out_proj
        self.pads = list(src.pads)

    def forward(self, x, *film):
        h = self.in_proj(x)
        for i, (conv, (pl, pr)) in enumerate(zip(self.convs, self.pads)):
            h = conv(nn.functional.pad(h, (pl, pr, 0, 0)))
            h = torch.tanh(film[2 * i] * h + film[2 * i + 1])
        return self.out_proj(h)


def film_params(src: RSNv2, emb_idx: int = 0):
    with torch.no_grad():
        e = src.emb(torch.tensor([emb_idx]))
        out = []
        for f in src.films:
            out.append(f.gamma(e).view(1, src.ch, 1, 1))
            out.append(f.delta(e).view(1, src.ch, 1, 1))
    return tuple(out)


def _qat_deploy(src, x, film):
    import torch.nn.functional as F_
    from .model import _fq_act, _fq_w
    q = src.qat
    h = _fq_act(x, *q["input"])
    h = _fq_act(F_.conv2d(h, _fq_w(src.in_proj.weight), src.in_proj.bias), *q["in_proj"])
    for i, (conv, (pl, pr)) in enumerate(zip(src.convs, src.pads)):
        g = _fq_act(film[2 * i], *q[f"gamma{i}"])
        d = _fq_act(film[2 * i + 1], *q[f"delta{i}"])
        c = _fq_act(F_.conv2d(F_.pad(h, (pl, pr, 0, 0)), _fq_w(conv.weight), conv.bias,
                              dilation=conv.dilation), *q[f"conv{i}"])
        m = _fq_act(g * c, *q[f"mul{i}"])
        a = _fq_act(m + d, *q[f"add{i}"])
        h = _fq_act(torch.tanh(a), *q[f"tanh{i}"])
    return _fq_act(F_.conv2d(h, _fq_w(src.out_proj.weight), src.out_proj.bias), *q["out"])


def qat_overrides(onnx_path, qat):
    import onnx
    m = onnx.load(onnx_path)
    prod = {o: n for n in m.graph.node for o in n.output}
    rng = lambda k: [{"rmin": np.array(min(float(qat[k][0]), 0.0), dtype=np.float32),
                      "rmax": np.array(max(float(qat[k][1]), 0.0), dtype=np.float32)}]
    ov = {"rsn_in": rng("input")}
    for n in m.graph.node:
        if n.op_type == "Conv" and n.input[1] == "in_proj.weight":
            ov[n.output[0]] = rng("in_proj")
        elif n.op_type == "Conv" and n.input[1].startswith("convs."):
            i = int(n.input[1].split(".")[1])
            ov[n.output[0]] = rng(f"conv{i}")
        elif n.op_type == "Conv" and n.input[1] == "out_proj.weight":
            ov[n.output[0]] = rng("out")
        elif n.op_type == "Mul":
            g = next(x for x in n.input if x.startswith("rsn_gamma"))
            i = int(g[len("rsn_gamma"):])
            ov[n.output[0]] = rng(f"mul{i}")
            ov[g] = rng(f"gamma{i}")
        elif n.op_type == "Add":
            d = next(x for x in n.input if x.startswith("rsn_delta"))
            i = int(d[len("rsn_delta"):])
            ov[n.output[0]] = rng(f"add{i}")
            ov[d] = rng(f"delta{i}")
    for n in m.graph.node:
        if n.op_type == "Tanh":
            i = int(prod[n.input[0]].input[1][len("rsn_delta"):]) if prod[n.input[0]].op_type == "Add" else None
            if i is not None:
                ov[n.output[0]] = rng(f"tanh{i}")
    for n in m.graph.node:
        if n.op_type == "Pad" and n.input[0] in ov:
            ov[n.output[0]] = ov[n.input[0]]
    expect = 1 + 1 + 6 * 6 + 1
    got = sum(1 for k in ov if not any(k == n.output[0] for n in m.graph.node if n.op_type == "Pad"))
    assert got == expect, f"mapped {got} QAT tensors, expected {expect}: {sorted(ov)}"
    return ov


def assert_hw_head(int8_path: str):
    import onnx
    m = onnx.load(int8_path)
    out_name = m.graph.output[0].name
    producer = next(n for n in m.graph.node if out_name in n.output)
    assert producer.op_type in ("QuantizeLinear", "DequantizeLinear", "Conv"), (
        f"graph ends on {producer.op_type}: a terminal activation compiles to a SW epoch.")
    print(f"    output head: terminates on {producer.op_type} (no SW-epoch trap)")


def real_windows(src: RSNv2, cache: str, n: int, seed: int = 0, lookahead: int = 0):
    items, meta = D.load_cache(cache, lookahead=lookahead)
    rng = np.random.default_rng(seed)
    out = []
    for _ in range(n):
        it = items[int(rng.integers(len(items)))]
        x, y, a, utt, valid = it
        T = x.shape[1]
        good = np.flatnonzero(valid)
        if len(good) == 0 or T < WINDOW:
            continue
        h = int(rng.choice(good))
        end = min(T, max(WINDOW, (h + 1) * 80))
        w = x[:, end - WINDOW:end].astype(np.float32)
        out.append((w.reshape(1, CIN, 1, WINDOW), film_params(src, int(utt)),
                    y[end - WINDOW:end].astype(np.float32)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", default=None)
    ap.add_argument("--out", default="rsn_v2/rsn")
    ap.add_argument("--calib-cache", dest="calib_cache", default=None,
                    help="cache dir with real calibration data")
    ap.add_argument("--calib-n", dest="calib_n", type=int, default=1000)
    ap.add_argument("--calib-method", dest="calib_method", choices=("minmax", "percentile"),
                    default="percentile",
                    help="percentile (default); MinMax follows every outlier and loses "
                         "a lot of accuracy")
    ap.add_argument("--calib-pct", dest="calib_pct", type=float, default=99.99)
    ap.add_argument("--check-n", dest="check_n", type=int, default=64,
                    help="real windows for the INT8-vs-float check")
    a = ap.parse_args()
    assert_rf()

    n_emb, arch, ck = 1, None, None
    if a.ckpt:
        ck = torch.load(a.ckpt, map_location="cpu", weights_only=False)
        n_emb = ck.get("n_emb", 1)
        arch = ck.get("arch")
    src = from_arch(arch, n_emb).eval()
    if not src.is_deployed_arch:
        raise SystemExit(f"\n{a.ckpt} was trained with a PROBE architecture {src.arch()}; "
                         f"it cannot be exported.")
    if ck is not None:
        src.load_state_dict(ck["model"])
    L = src.lookahead
    o0, o1 = output_slice(L)

    dep = RSNDeploy(src).eval()
    film = film_params(src, 0)
    x = torch.zeros(1, CIN, 1, WINDOW)
    film_names = [f"rsn_{'gamma' if i % 2 == 0 else 'delta'}{i // 2}" for i in range(len(film))]

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    f_onnx = f"{out}_float.onnx"
    torch.onnx.export(dep, (x, *film), f_onnx, opset_version=13,
                      input_names=["rsn_in"] + film_names, output_names=["rsn_out"])
    print(f"RSN deploy graph: window {WINDOW}, {src.ch} ch, dilations {src.dilations}, "
          f"{len(film)} FiLM inputs, LOOKAHEAD {L} samples ({L / 16:.2f} ms)")
    print(f"    per-layer pads (left,right): {src.pads}")
    print(f"    FIRMWARE: keep outputs [{o0}, {o1}) of each window (NOT the newest 80 unless L=0); "
          f"D_PATH += {3 * L} samples @48 kHz")
    print(f"-> {f_onnx}")

    from onnxruntime.quantization import (CalibrationDataReader, QuantFormat, QuantType,
                                          quantize_static)
    from onnxruntime.quantization.shape_inference import quant_pre_process

    wins = []
    if a.calib_cache:
        wins = real_windows(src, a.calib_cache, a.calib_n + a.check_n, lookahead=L)
        print(f"    calibration: {len(wins) - a.check_n} REAL windows from {a.calib_cache}")
    else:
        print("    calibration: RANDOM tensors (pass --calib-cache for real data)")

    class Calib(CalibrationDataReader):
        def __init__(self, n=64, seed=0):
            r = np.random.default_rng(seed)
            batches = []
            if wins:
                for w, fp, _ in wins[: max(1, len(wins) - a.check_n)]:
                    b = {"rsn_in": w}
                    for nm, v in zip(film_names, fp):
                        b[nm] = v.numpy().astype(np.float32)
                    batches.append(b)
            else:
                for _ in range(n):
                    b = {"rsn_in": r.uniform(-1, 1, (1, CIN, 1, WINDOW)).astype(np.float32)}
                    for nm, v in zip(film_names, film):
                        b[nm] = (v.numpy() + r.normal(0, 0.05, v.shape).astype(np.float32))
                    batches.append(b)
            self.it = iter(batches)

        def get_next(self):
            return next(self.it, None)

    pre, i8 = f"{out}_int8.pre.onnx", f"{out}_int8.onnx"
    quant_pre_process(f_onnx, pre, skip_symbolic_shape=True)
    from onnxruntime.quantization import CalibrationMethod
    cm = {"minmax": CalibrationMethod.MinMax, "percentile": CalibrationMethod.Percentile}[a.calib_method]
    extra = {"CalibPercentile": a.calib_pct} if a.calib_method == "percentile" else {}
    if ck is not None and ck.get("qat"):
        ov = qat_overrides(pre, ck["qat"])
        extra["TensorQuantOverrides"] = ov
        print(f"    QAT checkpoint: {len(ov)} activation ranges pinned to the ones it trained "
              f"under (TensorQuantOverrides) -- calibration only fills the rest")
    print(f"    activation calibration: {a.calib_method}"
          + (f" {a.calib_pct}" if extra else ""))
    quantize_static(pre, i8, Calib(), quant_format=QuantFormat.QDQ,
                    activation_type=QuantType.QInt8, weight_type=QuantType.QInt8,
                    per_channel=True, calibrate_method=cm, extra_options=extra)
    assert_hw_head(i8)
    import onnx
    ops = {}
    for n in onnx.load(i8).graph.node:
        ops[n.op_type] = ops.get(n.op_type, 0) + 1
    print(f"-> {i8}")
    print("op histogram:", dict(sorted(ops.items())))

    if wins:
        import onnxruntime as ort
        sf_ = ort.InferenceSession(f_onnx)
        _so = ort.SessionOptions()
        _so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
        si = ort.InferenceSession(i8, _so)
        snr_f, snr_q, snr_qt, rho_f, rho_q = [], [], [], [], []
        for w, fp, y in wins[-a.check_n:]:
            feed = {"rsn_in": w}
            for nm, v in zip(film_names, fp):
                feed[nm] = v.numpy().astype(np.float32)
            yf = sf_.run(None, feed)[0].reshape(-1)[o0:o1]
            yq = si.run(None, feed)[0].reshape(-1)[o0:o1]
            yt = y[o0:o1]
            snr_f.append(10 * np.log10(np.sum(yt ** 2) / max(np.sum((yt - yf) ** 2), 1e-20)))
            snr_qt.append(10 * np.log10(np.sum(yt ** 2) / max(np.sum((yt - yq) ** 2), 1e-20)))
            rho_f.append(float(np.dot(yf, yt) / (np.linalg.norm(yf) * np.linalg.norm(yt) + 1e-20)))
            rho_q.append(float(np.dot(yq, yt) / (np.linalg.norm(yq) * np.linalg.norm(yt) + 1e-20)))
            snr_q.append(10 * np.log10(np.sum(yf ** 2) / max(np.sum((yf - yq) ** 2), 1e-20)))
        if ck is not None and ck.get("qat"):
            src.qat = {k: tuple(v) for k, v in ck["qat"].items()}
            snr_s, snr_st = [], []
            with torch.no_grad():
                for w, fp, y in wins[-a.check_n:]:
                    feed = {"rsn_in": w}
                    for nm, v in zip(film_names, fp):
                        feed[nm] = v.numpy().astype(np.float32)
                    yq = si.run(None, feed)[0].reshape(-1)[o0:o1]
                    u = torch.tensor([0])
                    ys = _qat_deploy(src, torch.from_numpy(w), fp).reshape(-1)[o0:o1].numpy()
                    yt = y[o0:o1]
                    snr_s.append(10 * np.log10(np.sum(ys ** 2) / max(np.sum((ys - yq) ** 2), 1e-20)))
                    snr_st.append(10 * np.log10(np.sum(yt ** 2) / max(np.sum((yt - ys) ** 2), 1e-20)))
            src.qat = None
            print(f"    INT8 vs QAT-SIM on {len(snr_s)} real blocks: median {np.median(snr_s):.1f} dB "
                  f"(export faithful to training if high)")
            print(f"    QAT-SIM vs TARGET: median {np.median(snr_st):.1f} dB")
        print(f"    float vs TARGET on {len(snr_f)} real blocks: median {np.median(snr_f):.1f} dB")
        print(f"    INT8  vs TARGET on {len(snr_qt)} real blocks: median {np.median(snr_qt):.2f} dB, "
              f"rho {np.median(rho_q):.3f}   (float: {np.median(snr_f):.2f} dB, rho {np.median(rho_f):.3f})"
              f"  -> quality parity {'OK' if np.median(rho_q) >= np.median(rho_f) - 0.01 else 'LOST'}")
        print(f"    INT8 vs FLOAT  on {len(snr_q)} real blocks: median {np.median(snr_q):.1f} dB, "
              f"p10 {np.percentile(snr_q, 10):.1f} dB")


if __name__ == "__main__":
    main()

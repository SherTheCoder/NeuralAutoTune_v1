"""Pitch accuracy (RPA/RCA within 50 cents) of a checkpoint or an INT8 model.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import argparse
import json
from pathlib import Path

import numpy as np

from .data import FrameDataset
from .decode import decode
from .frontend import bin_to_f0

def _metrics(b_true, f0_pred, voiced_true, voice_prob):
    v = voiced_true.astype(bool) & (b_true >= 0)
    err_c = 1200.0 * np.log2(f0_pred[v] / bin_to_f0(b_true[v]))
    chroma = np.abs((err_c + 600.0) % 1200.0 - 600.0)
    return {
        "rpa50": float(np.mean(np.abs(err_c) <= 50.0)),
        "rca50": float(np.mean(chroma <= 50.0)),
        "med_cents": float(np.median(np.abs(err_c))),
        "voice_acc": float(np.mean((voice_prob > 0.5) == voiced_true.astype(bool))),
        "n_voiced": int(v.sum()),
    }

def evaluate_model(model, ds, dev, batch=256):
    import torch
    model.eval()
    logits_all, vprob_all = [], []
    with torch.no_grad():
        for i in range(0, len(ds), batch):
            feats = np.stack([ds[j][0] for j in range(i, min(i + batch, len(ds)))])
            x = torch.from_numpy(feats).to(dev)[:, None, None, :]
            lg, vl = model(x)
            logits_all.append(lg.cpu().numpy())
            vprob_all.append(torch.sigmoid(vl[:, 0]).cpu().numpy())
    model.train()
    logits = np.concatenate(logits_all)
    f0, _, _ = decode(logits)
    return _metrics(ds.bin, f0, ds.voiced, np.concatenate(vprob_all))

def evaluate_tflite(tflite_path, ds):
    try:
        from ai_edge_litert.interpreter import Interpreter
    except ImportError:
        from tensorflow.lite.python.interpreter import Interpreter
    ip = Interpreter(model_path=str(tflite_path))
    ip.allocate_tensors()
    inp = ip.get_input_details()[0]
    outs = ip.get_output_details()
    pitch_out = max(outs, key=lambda d: int(np.prod(d["shape"])))
    voice_out = min(outs, key=lambda d: int(np.prod(d["shape"])))
    logits, vprob = [], []
    for i in range(len(ds)):
        feat = ds[i][0].reshape(inp["shape"])
        if inp["dtype"] == np.int8:
            s, z = inp["quantization"]
            feat = np.clip(np.round(feat / s + z), -128, 127).astype(np.int8)
        ip.set_tensor(inp["index"], feat)
        ip.invoke()
        def deq(d):
            y = ip.get_tensor(d["index"]).astype(np.float32)
            s, z = d["quantization"]
            return (y - z) * s if s else y
        logits.append(deq(pitch_out).reshape(-1))
        vprob.append(1.0 / (1.0 + np.exp(-float(deq(voice_out).reshape(-1)[0]))))
    f0, _, _ = decode(np.stack(logits))
    return _metrics(ds.bin, f0, ds.voiced, np.asarray(vprob))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", type=Path, required=True)
    ap.add_argument("--ckpt", type=Path)
    ap.add_argument("--tflite", type=Path)
    ap.add_argument("--val-singers", default="abjones,amy,ADIZ,SAMF")
    a = ap.parse_args()
    ds = FrameDataset(a.cache, singers=set(a.val_singers.split(",")),
                      augment=False)
    print(f"val: {len(ds)} frames, singers {ds.singers}")
    report = {}
    if a.ckpt:
        import torch
        from .model import SwiftF0
        dev = ("cuda" if torch.cuda.is_available() else
               "mps" if torch.backends.mps.is_available() else "cpu")
        model = SwiftF0().to(dev)
        model.load_state_dict(torch.load(a.ckpt, map_location=dev))
        report["float"] = evaluate_model(model, ds, dev)
        print("float:", json.dumps(report["float"], indent=1))
    if a.tflite:
        report["int8"] = evaluate_tflite(a.tflite, ds)
        print("int8 :", json.dumps(report["int8"], indent=1))
        if "float" in report:
            drop = report["float"]["rca50"] - report["int8"]["rca50"]
            gate = report["int8"]["rca50"] >= 0.93 and drop < 0.02
            print(f"int8 RCA@50c "
                  f"{report['int8']['rca50']:.2%} (>=93%), drop "
                  f"{100 * drop:.2f} pp (<2) -> {'PASS' if gate else 'FAIL'}")
    Path("eval_report.json").write_text(json.dumps(report, indent=1))

if __name__ == "__main__":
    main()

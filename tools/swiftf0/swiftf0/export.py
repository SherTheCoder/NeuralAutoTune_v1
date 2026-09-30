"""Export the pitch network to ONNX (and TFLite INT8).

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import argparse
from pathlib import Path

import numpy as np
import torch

from .model import NBINS_IN, SwiftF0

def export_onnx(ckpt: Path, out_dir: Path) -> Path:
    model = SwiftF0()
    model.load_state_dict(torch.load(ckpt, map_location="cpu"))
    model.eval()
    onnx_path = out_dir / "swiftf0_float.onnx"
    dummy = torch.zeros(1, 1, 1, NBINS_IN)
    torch.onnx.export(model, dummy, onnx_path, opset_version=13,
                      input_names=["features"],
                      output_names=["pitch_logits", "voice_logit"])
    try:
        import onnxruntime as ort
        x = np.random.default_rng(0).uniform(-1, 1, (1, 1, 1, NBINS_IN)) \
            .astype(np.float32)
        with torch.no_grad():
            tp, tv = model(torch.from_numpy(x))
        op, ov = ort.InferenceSession(str(onnx_path)).run(None, {"features": x})
        d = max(float(np.abs(tp.numpy() - op).max()),
                float(np.abs(tv.numpy() - ov).max()))
        print(f"onnx parity max|diff| = {d:.2e} ({'OK' if d < 1e-4 else 'BAD'})")
    except ImportError:
        print("onnxruntime not installed; parity check skipped")
    print(f"-> {onnx_path} ({onnx_path.stat().st_size / 1024:.0f} KB)")
    return onnx_path

def export_tflite(onnx_path: Path, out_dir: Path, representative: Path):
    import subprocess
    import sys
    import tensorflow as tf
    sm_dir = out_dir / "saved_model"
    subprocess.run([sys.executable, "-m", "onnx2tf", "-i", str(onnx_path),
                    "-o", str(sm_dir), "-nuo", "--non_verbose"], check=True)
    rep = np.load(representative)
    conv = tf.lite.TFLiteConverter.from_saved_model(str(sm_dir))
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = _make_rep(rep, sm_dir)
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    tfl = conv.convert()
    tfl_path = out_dir / "swiftf0_int8.tflite"
    tfl_path.write_bytes(tfl)
    print(f"-> {tfl_path} ({len(tfl) / 1024:.0f} KB)")
    return tfl_path

def _make_rep(rep, sm_dir):
    import tensorflow as tf
    sm = tf.saved_model.load(str(sm_dir))
    shape = list(sm.signatures["serving_default"]
                 .structured_input_signature[1].values())[0].shape.as_list()
    def gen():
        for row in rep:
            yield [row.reshape([1 if s is None else s for s in shape])
                   .astype(np.float32)]
    return gen

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=Path("artifacts"))
    ap.add_argument("--tflite", action="store_true")
    ap.add_argument("--representative", type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    onnx_path = export_onnx(a.ckpt, a.out)
    if a.tflite:
        if not a.representative:
            raise SystemExit("--tflite requires --representative NPY")
        export_tflite(onnx_path, a.out, a.representative)

if __name__ == "__main__":
    main()

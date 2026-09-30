# RSN training

The RSN takes four channels over a 352-sample window at 16 kHz: the
excitation at the target pitch, the singer's LPC residual, and the source and
target pitch. It outputs the residual re-timed to the target pitch. The
training target is an LP-PSOLA pitch shift of the singer's own residual, with
the glottal pulses placed where the board's oscillator would place them.

Needs Python 3 with torch, numpy, scipy, soundfile and onnxruntime. The
corpus used was VocalSet (not included).

```
cd tools/rsn_v3

# training cache: pitch tracker, LPC residual, glottal closures, target contours
python -m rsn.prep --corpus <VocalSet/FULL> --out cache \
    --swiftf0 swiftf0/artifacts/artifacts/swiftf0_float.onnx \
    --items-per-file 2 --max-secs 4 --exclude fry inhaled spoken lip_trill

# training, then an optional adversarial stage, then quantization-aware training
python -m rsn.train --cache cache --out ckpt/full --steps 150000 --batch 32 --amp
python -m rsn.train --cache cache --out ckpt/gan --gan --init-from ckpt/full/rsn.pt --steps 30000 --lr 5e-5
python -m rsn.train --cache cache --out ckpt/qat --qat --init-from ckpt/gan/rsn.pt --steps 15000

# INT8 ONNX, then the package the firmware uses
python -m rsn.export --ckpt ckpt/qat/rsn.pt --out ckpt/rsn --calib-cache cache
python -m rsn.package --ckpt ckpt/qat/rsn.pt --int8 ckpt/rsn_int8.onnx --out ../../deploy/rsn_new --cache cache
```

The package then goes through `tools/board/build_rsn_ec.sh` and
`gen_rsn_fw.py` (see `tools/board/README.md`).

`prep.py` prints the residual scale it measured. The firmware and every later
step must use that value; the deployed network uses 19.6114.

| | |
|---|---|
| `rsn/model.py` | the network and quantization-aware training |
| `rsn/dsp.py` | LPC, residual, excitation, synthesis, shared with the firmware |
| `rsn/psola.py` | glottal closures and LP-PSOLA targets |
| `rsn/f0logic.py` | target pitch contours |
| `rsn/data.py`, `rsn/prep.py` | dataset and cache |
| `rsn/losses.py`, `rsn/gan.py` | losses and discriminators |
| `rsn/train.py`, `rsn/export.py`, `rsn/package.py`, `rsn/calibrate.py` | training and deployment |
| `excitation/excitation_twin.py` | Python copy of the board's oscillator |
| `c1/` | checking the board's C code against the Python models |

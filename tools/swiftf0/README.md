# Pitch tracker training

A SwiftF0-style network. Unlike the published SwiftF0, the STFT is not part
of the network: the front-end (Hann window, 1024-point FFT, projection onto a
20-cent log-frequency grid, log) runs on the CPU, because the NPU has no FFT.
The network is a stack of 1x5 convolutions over the grid, 93k parameters,
with 360 pitch outputs and one voicing output. The pitch is decoded as a
weighted mean around the peak.

`swiftf0/frontend.py` and `swiftf0/decode.py` are the reference for
`swiftf0_frontend.c` and `swiftf0_decode.c`, and `gen_tables.py` writes
`swiftf0_tables.h` from them.

Training data: MIR-1K and NUS-48E (not included).

```
cd tools/swiftf0
pip install -r requirements.txt

python -m swiftf0.data prepare --mir1k <MIR-1K> --nus48e <NUS-48E> --out cache/
python -m swiftf0.data representative --cache cache/ --out representative.npy
python -m swiftf0.train --cache cache/ --out runs/b1 --steps 30000
python -m swiftf0.export --ckpt runs/b1/best.pt --out artifacts --tflite --representative representative.npy
python -m swiftf0.eval --cache cache/ --ckpt runs/b1/best.pt --tflite artifacts/swiftf0_int8.tflite
```

The NPU build starts from `npu_build/swiftf0_int8_phasegrid.onnx`, the INT8
graph with its dilated convolutions rewritten by `swiftf0/npu_rewrite.py`;
`tools/board/build_sf0_ec.sh` compiles it for the board.

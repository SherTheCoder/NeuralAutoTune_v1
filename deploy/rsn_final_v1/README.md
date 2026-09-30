# Deployed RSN

The network running on the board: 96 channels, dilations 1 to 32, no
lookahead, INT8 with quantization-aware training (15000 steps).

| | |
|---|---|
| window / hop | 352 / 80 samples at 16 kHz |
| output used | samples 272 to 351 of each window |
| residual scale | 19.6114 (applied to the residual input, divided out of the output) |
| input quantization | scale 0.0133494, zero point -1 |
| output quantization | scale 0.0100028, zero point -77 |
| singer embedding | mean of the trained table |

Files:

- `rsn_int8.onnx`: the INT8 graph
- `npu_gen/`, `npu_gen_ec/`: code generated for the NPU, plain and epoch-controller
- `rsn_film.h`: the FiLM inputs for the embedding
- `rsn_golden.h`, `golden.npz`: input windows and the host graph's outputs, used by the boot self-test
- `listen/`: examples, see `listen/INDEX.md`

The training checkpoint is not included because of its size.

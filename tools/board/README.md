# Board tools

Everything here runs on the host with the board connected through its ST-LINK
USB port. The board is in DEV boot mode: the debugger loads both images into
RAM and nothing is written to flash.

## Running

```
tools/board/board.sh load      # build, load, run
tools/board/board.sh log 10    # show 10 s of console output
```

At boot you should see the kernel banner, the RSN self-test and then a status
line every second:

```
microT-Kernel Version 3.00
RSN: golden 8 windows x 352: ... max |d| 3 LSB (tol 8), mean |d| 0.387 LSB (tol 1.000) -> PASS
Voice: RSN | rsn 200 ... | miss 0 (t1 0 t2 0) | ...
```

`rsn 200` means 200 hops a second went through the RSN, `miss 0` that none
were late. If the self-test fails, the firmware plays the uncorrected LPC
round trip instead; reloading with `board.sh load` rewrites the weights.

`blobs/rsn_weights.elf` holds the same weights for the STM32CubeIDE launch,
which loads them before the FSBL runs (its init commands power AXISRAM4
first). After changing `rsn_weights.bin`, regenerate it from this folder:

```
arm-none-eabi-objcopy -I binary -O elf32-littlearm -B arm --change-addresses 0x34270000 \
  --rename-section .data=.rsn_weights,alloc,load,readonly,data,contents \
  blobs/rsn_weights.bin blobs/rsn_weights.elf
```

## Controls

On the board, a short press on USER1 steps through the keys (chromatic, eight
major keys, four minor keys) and a long press switches correction off and on.
LD1 is on while correcting and blinks in bypass; LD2 flashes on clipping or a
late hop.

On the serial port (2000000 8N1, or `bench.py cmd "<command>"`):

| command | |
|---|---|
| `mode rsn`, `mode plain`, `mode bypass` | corrected, resynthesis only, dry |
| `key 0..11`, `scale chrom\|major\|minor\|<hex>` | target notes |
| `vib ratio\|snap\|raw` | keep vibrato around the note, flatten it, or hold the sung centre |
| `hf 0\|1`, `hfcomp 0\|1`, `trim <n>` | band above 7 kHz, how it is split, alignment in samples |
| `agc 0\|1`, `agct <x100>`, `makeup <x10 dB>` | RSN input level control, its target, output trim |
| `route 0\|1` | 1: unvoiced sounds play dry (default) |
| `stat` | mode, late hops, overruns, UART errors, stack headroom |

## Recording

```
tools/board/bench.py record --secs 60 --name take1          # output, 16-bit 48 kHz
tools/board/bench.py record --secs 60 --both --name take1   # input and output, 12-bit
```

Ctrl-C stops early and still saves. The files go to `captures/`.

## Tests

```
tools/board/bench.py vowel 227                   # vowel 45 cents flat of A#3
tools/board/bench.py seq 400 212 240 258 285     # note sequence
tools/board/bench.py clip deploy/rsn_final_v1/listen/sustained_03_source.wav
tools/board/bench.py latency
tools/board/bench.py d3                          # flatness of the band recombination
tools/board/soak.py --minutes 30
```

The bench feeds its test signal in place of the microphone, so the whole
chain processes it as if it had been sung.

Analog latency: press a headphone earcup onto the board's microphone and run
`bench.py loopback`. The board clicks its output and records its own mic;
the printed echo delay plus 8.85 ms is the end-to-end latency.

## Rebuilding the networks

```
tools/board/build_rsn_ec.sh deploy/rsn_final_v1
python3 tools/board/gen_rsn_fw.py --deploy deploy/rsn_final_v1 --gen deploy/rsn_final_v1/npu_gen_ec
tools/board/build_sf0_ec.sh
```

These need ST Edge AI 4.0 in `/Applications/ST/STEdgeAI/4.0`.

## Calibrating the RSN to a singer

Optional; on held-out VocalSet singers it made almost no difference.

1. Record 3 to 5 minutes of scales, 48 kHz mono WAV, into
   `recordings/<name>/`.
2. From `tools/rsn_v3`:
   ```
   python -m rsn.prep --corpus recordings --out c5_cache \
       --swiftf0 swiftf0/artifacts/artifacts/swiftf0_float.onnx \
       --items-per-file 4 --max-secs 6 --res-scale 19.61141013589234 --val-frac 0
   python -m rsn.calibrate --ckpt <checkpoint> --cache c5_cache --singer <name> --out c5_<name>.json
   python -m rsn.package --ckpt <checkpoint> --int8 ../../deploy/rsn_final_v1/rsn_int8.onnx \
       --out ../../deploy/rsn_<name> --emb-json c5_<name>.json --cache c5_cache
   ```
   Keep `--res-scale` at 19.6114; the network was trained with it.
3. `python3 tools/board/gen_rsn_fw.py --deploy deploy/rsn_<name> --gen deploy/rsn_final_v1/npu_gen_ec`,
   then `board.sh load`.

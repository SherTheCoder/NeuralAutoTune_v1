# Measurements

STM32N6570-DK, CPU at 600 MHz, NPU at 1 GHz, 30 September 2026. The test
bench (`tools/board/bench.py`) replaces the microphone samples inside the
capture interrupt, so everything downstream sees the test signal as if it
came from the microphone, and records input and output.

## Boot

```
microT-Kernel Version 3.00
NeuralAutoTune 1.0 on micro T-Kernel 3.00: natune middleware sample
RSN: NPU image (weights + EC blob) @0x24270000 338624 B, CRC32 0x82FFC619 (want 0x82FFC619) OK
RSN: golden 8 windows x 352: 1052 bytes differ, max |d| 3 LSB (tol 8), mean |d| 0.387 LSB (tol 1.000) -> PASS
RSN: inference (50) min 1736799 / mean 1739684 / max 1741467 cyc = 2894 / 2899 / 2902 us @ 600 MHz
```

The full boot log is in `logs/boot_console.log`.

## Running

Once a second the firmware prints a status line:

```
Voice: RSN | rsn 200 plain 0 drop 0 | lat max 3161 us (D 4 ms) margin min 840 us ... | miss 0 ...
BUDGET worst/pass 368620 cyc (614 us of 1000) | ... | overruns 0 | stack free 4040 B
```

- 200 RSN hops per second, none late.
- A hop is ready 3.16 ms after its input, 0.84 ms before it has to play.
- The slowest audio task pass took 614 µs of its 1 ms.

## Test bench

| test | result |
|---|---|
| `bench.py vowel 227` (45 cents flat of A#3) | 227.06 Hz in, 233.01 Hz out, 0.5 cents from A#3 |
| `bench.py clip .../sustained_03_source.wav` (VocalSet) | 36.3 cents off the scale in, 2.2 out (median), level -0.4 dB |
| `bench.py seq 400 212 240 258 285 --ms 1800` | 36 -> 3.6, 49 -> 5.7, 24 -> 16 cents (the last segment includes a note change) |
| `bench.py latency` | 424-425 samples, 8.85 ms from mic block to speaker block |
| `bench.py d3` (5-9 kHz flatness) | within 0.37 dB |
| `bench.py record --secs 20 --both` | no missing or corrupt frames |
| `bench.py cmd stat` afterwards | misses 0, overruns 0, UART errors 0 |

## 30-minute soak

`tools/board/soak.py --minutes 30` switches the input every two minutes
between the live mic, a note sequence, a gated vowel, a vowel with vibrato, a
real recording and bypass.

| run | hop latency max | margin min | missed blocks | overruns | worst pass | stack left |
|---|---|---|---|---|---|---|
| 01:46 | 3204 µs | 781 µs | 0 of ~1.8 M | 0 | 663 µs | 3968 B |
| 02:22 | 3202 µs | 773 µs | 0 of ~1.8 M | 0 | 691 µs | 3968 B |
| 09:14, this release | 3182 µs | 782 µs | 0 of ~1.8 M | 0 | 661 µs | 3968 B |

The first two ran on earlier builds of the same code. Logs and summaries are
in `logs/`. For comparison, the bare-metal version's
soak also had no missed blocks, with a worst pass of 629 µs.

## Memory

| | used | size |
|---|---|---|
| code and constants | 305 KB | 511 KB |
| RAM | 323 KB | 400 KB |
| non-cacheable RAM (DMA, capture buffers) | 217 KB | 224 KB |

μT-Kernel itself uses 12.6 KB of code and 37 KB of RAM (32 KB of it is the
system memory pool). The three natune task stacks are 6, 8 and 8 KB, and at
least 3968 bytes of each were never touched.

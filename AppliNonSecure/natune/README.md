# natune

Pitch-correction middleware for μT-Kernel 3.0 on the STM32N6.

```c
#include "natune.h"

EXPORT INT usermain(void)
{
    T_NATUNE_CFG cfg = {0};                 /* defaults */
    natune_init(&cfg);
    natune_start();
    natune_set_key(9, NATUNE_SCALE_MINOR);  /* A minor */
    for (;;) {
        T_NATUNE_STS sts;
        natune_ref_sts(&sts);
        tk_dly_tsk(1000);
    }
}
```

## Files

```
include/natune.h        public API
src/core/               tasks, kernel objects, API
src/dsp/                decimation, LPC, lattice filters, F0 logic, voicing
                        gate, HF band, voice path
src/pitch/              pitch tracker front-end, NPU part, decoder
src/npu/                NPU service task, RSN runtime and self-test
src/diag/               serial commands, test bench, capture for the Python
                        cross-check
port/stm32n6570dk/      audio DMA, WM8904 codec, console, T-Monitor output
```

## Tasks and interrupts

| | priority | |
|---|---|---|
| npu task | 2 | owns the NPU runtime; runs the RSN before the pitch tracker and synthesizes each hop |
| audio task | 3 | one pass per 1 ms block |
| ctrl task | 12 | serial commands and status lines |
| GPDMA1 ch0/1 irq | 1 | audio capture and playback |
| USART1 irq | 3 | console |
| NPU0 irq | 4 | NPU completion |

Interrupts that call the kernel have to be at priority 1 or lower, because
the kernel masks from 1 up in its critical sections. The audio and ctrl tasks
share a mutex (TA_INHERIT). The npu task only exchanges data with them through
single-producer queues, so it can preempt either. A 1 ms cyclic handler drives
`HAL_GetTick`, and `HAL_Delay` calls `tk_dly_tsk` when used from a task.

## API

All calls return `E_OK` or a negative error code, and return `E_CTX` if
called from an interrupt handler.

**`natune_init(const T_NATUNE_CFG *cfg)`** sets up the codec, the DSP and
both networks, runs the RSN self-test and starts the tasks. It takes about
half a second. If the self-test fails, the voice path falls back to the plain
LPC round trip. `cfg` can be NULL; zero fields take defaults.

| field | default |
|---|---|
| `npu_pri`, `audio_pri`, `ctrl_pri` | 2, 3, 12 (must be increasing) |
| `flags` | `NATUNE_CFG_NOSHELL`, `NATUNE_CFG_QUIET`, `NATUNE_CFG_MODE` |
| `mode` | used with `NATUNE_CFG_MODE`, otherwise RSN |
| `key_root` | 0 (C) |
| `scale_mask` | chromatic |

**`natune_start()`** starts the microphone. The audio task starts the
speaker once the first hop is ready, aligned to the microphone blocks.

**`natune_set_mode(mode)`**: `NATUNE_MODE_RSN` (corrected), `NATUNE_MODE_PLAIN`
(analysis and resynthesis only) or `NATUNE_MODE_BYPASS` (dry). All paths keep
running, so switching does not click.

**`natune_set_key(root, mask)`**: root 0..11, mask bit i is the semitone i
above the root. `NATUNE_SCALE_CHROMATIC`, `_MAJOR` and `_MINOR` are defined.

**`natune_set_vibrato(vib)`**: `NATUNE_VIB_RATIO` keeps the singer's vibrato
around the target note (default), `NATUNE_VIB_SNAP` flattens it,
`NATUNE_VIB_RAW` holds the sung centre.

**`natune_ref_sts(T_NATUNE_STS *sts)`** returns the mode, whether the RSN
passed its self-test, the key, counts of blocks, hops, late hops, dropped
blocks and clipped blocks, the latest input and corrected pitch, and the
smallest stack headroom of the three tasks.

## Serial commands

On USART1 (the ST-LINK virtual COM port) at 2 Mbit/s: `mode`, `key`, `scale`,
`vib`, `stat`, and test commands that replace the microphone with a
synthetic voice or an uploaded recording, capture input and output, or
stream them. `bench.h` lists them all; `tools/board/bench.py` uses them.

## Porting

A new board needs its own `port/<board>/natune_port.c` implementing
`natune_port.h`. Its capture interrupt calls `natune_isr_block()` for every
48-sample block. The rest assumes an STM32N6 with the Neural-ART runtime.

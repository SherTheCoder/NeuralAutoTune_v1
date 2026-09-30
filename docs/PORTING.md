# Porting notes

The STM32N6 boots a secure first stage (`FSBL/`). It sets the clocks, powers
the NPU and opens the memory firewalls, then jumps to the non-secure
application image, which is where μT-Kernel runs. BSP2 targets the secure
state on this chip, so a few settings were changed.

## Kernel configuration

In `AppliNonSecure/mtk3_bsp2/`:

- `include/sys/sysdepend/stm32_cube/discovery_stm32n657/sysdef.h`:
  `TRUSTZONE_SECURE 0`. This selects the non-secure exception return value and
  peripheral addresses.
- `include/sys/sysdepend/stm32_cube/cpu/stm32n6/sysdef.h`: the non-secure RAM
  is 0x24000000, 400 KB.
- `config/config.h`: 1 ms system tick.
- `config/config_bsp/stm32_cube/config_bsp.h`: 32 KB static system memory and
  `USE_SPMON` (a hardware stack limit per task).
- `config/config_tm.h`: `TM_COM_SERIAL_DEV 0`. T-Monitor output goes through
  the application's console (`natune/port/stm32n6570dk/tm_com.c`).
- `mtkernel/include/sys/inittask.h`: 8 KB stack for the initial task, since
  `usermain` runs `natune_init`.

The kernel API is unchanged.

## Two problems

**BSP2 v1.00.04 does not build on armv8m.** `sysdepend/stm32_cube/cpu/core/armv8m/interrupt.c`
declares `hllint_tbl[sN_INTVEC]`. It should be `N_INTVEC`, and without the fix
`tk_def_int` with `TA_HLNG` cannot be used on the Cortex-M33/M55 targets.

**The RAM vector table has to be non-cacheable.** The kernel copies the
vector table to RAM, and `tk_def_int` writes handler addresses into it. With
the data cache on, those writes stay in the cache, but the core reads vectors
from memory on exception entry. The first interrupt after the kernel started
used a stale vector and faulted. `MPU_Config()` in `Core/Src/main.c` now makes
the `.mtk_exctbl` section non-cacheable. Other μT-Kernel ports on cached
Cortex-M7/M55 parts could run into the same thing.

## Interrupts

The kernel masks interrupts of priority 1 and up in its critical sections,
so handlers that call it must not be at 0. The audio DMA moved from 0 to 1.

| interrupt | priority | calls the kernel |
|---|---|---|
| GPDMA1 ch0/ch1 | 1 | yes, tk_set_flg |
| USART1 | 3 | no |
| NPU0 | 4 | yes, tk_set_flg |

## From the bare-metal version

The earlier firmware was a main loop plus a PendSV handler for the NPU:

| bare metal | μT-Kernel |
|---|---|
| main loop waiting in WFI | audio task waiting on an event flag |
| PendSV handler for the NPU | npu task, above the audio task |
| shell and LEDs at the end of the loop | ctrl task and the application task |
| one loop, so no locking | mutex between audio and ctrl |
| HAL SysTick | kernel SysTick, 1 ms cyclic handler for the HAL tick |

The priorities keep the same order as before, so the timing is the same. The
NPU completion is picked up in 2 to 3 µs instead of 1 µs.

The idle loop does not use WFI. The kernel calls `low_pow()` with interrupts
masked, and sleeping would also need every clock's sleep enable. The NPU
runtime still uses WFE during the self-test, which is why those enables are
set.

/*
 * natune_port.h - what a board port has to provide
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef NATUNE_PORT_H
#define NATUNE_PORT_H

#include <tk/tkernel.h>
#include <stdint.h>

#define NATUNE_BLOCK		48U	/* samples per DMA half, 1 ms at 48 kHz */

IMPORT ER   natune_port_kernel_init(void);	/* interrupts, HAL time base */
IMPORT ER   natune_port_codec_init(void);
IMPORT ER   natune_port_start_capture(void);
IMPORT ER   natune_port_start_playback(void);

IMPORT int32_t *natune_port_mic_half(UINT h);	/* h: 0 ping, 1 pong */
IMPORT int16_t *natune_port_spk_half(UINT h);

/* TRUE if the speaker DMA is reading the half that spk belongs to */
IMPORT BOOL natune_port_tx_collision(const int16_t *spk);

/* implemented by the core, called by the capture DMA interrupt */
IMPORT void natune_isr_block(int32_t *half, UINT h);

#endif /* NATUNE_PORT_H */

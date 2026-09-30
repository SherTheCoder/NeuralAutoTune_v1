/*
 * rsn_npu.h - the RSN on the Neural-ART
 *
 * The network code is generated (X-CUBE-AI/network/rsn.c). Its weights are
 * too big for the application image, so board.sh writes them to AXISRAM4 and
 * rsn_npu_init checks their CRC before enabling the network.
 *
 * Inputs: the int8 window [4][352], then 12 FiLM vectors of 96 bytes
 * (gamma0, delta0 .. gamma5, delta5). Output: 352 int8, of which
 * [RSN_OUT0, RSN_OUT1) is the new hop.
 *
 * The pitch tracker shares the activation memory, so the FiLM inputs are
 * rewritten before every run and the two networks never overlap.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RSN_NPU_H
#define RSN_NPU_H

#include <stdint.h>

/* CRC check, buffer setup and golden self-test; blocks ~0.2 s.
 * Returns 1 if the RSN can be used. */
uint8_t rsn_npu_init(void);
uint8_t rsn_npu_ready(void);

int8_t *rsn_npu_input(void);

void    rsn_npu_start(void);
uint8_t rsn_npu_step(void);            /* returns 1 when done */
uint8_t rsn_npu_busy(void);
const int8_t *rsn_npu_output(void);

uint32_t rsn_npu_cycles_last(void);
uint32_t rsn_npu_cycles_max(void);
uint32_t rsn_npu_count(void);
/* worst launch -> interrupt and interrupt -> retired times, in cycles */
void     rsn_npu_timing(uint32_t *npu_max, uint32_t *pickup_max, uint8_t reset);

#endif /* RSN_NPU_H */

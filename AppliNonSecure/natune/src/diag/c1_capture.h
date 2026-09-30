/*
 * c1_capture.h - capture for checking the C code against the Python models
 *
 * Records 1.25 s of the raw input, the 16 kHz stream, the per-hop residual
 * and excitation, the pitch-tracker features, and the starting state of
 * each stage, then prints it as "C1|" hex lines. tools/rsn_v3/c1/c1_parse.py
 * and c1_crossval.py rerun the Python versions on the same data and compare.
 *
 * Arms itself after 300 ms of continuous voicing (c1_auto), or set c1_arm.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef C1_CAPTURE_H
#define C1_CAPTURE_H

#include <stdint.h>
#include "arm_math.h"

#define C1_HOPS      250u                 /* 1.25 s */
#define C1_HOP16     80u
#define C1_RAW48N    (C1_HOPS * 240u)
#define C1_DEC16N    (C1_HOPS * C1_HOP16)
#define C1_TICKS     128u                 /* pitch-tracker frames */
#define C1_XPHIST    240u
#define C1_DECHIST   47u
#define C1_SF0RING   1024u

void C1_Init(void);

/* hooks, no-ops unless a capture is running */
void C1_PushRaw48(const int32_t *mic, uint32_t n);
void C1_PushDec16(const float32_t *dec, uint32_t n);
void C1_HopHook(const float32_t *e80, const float32_t *exc80,
                float32_t f0_corr, float32_t exc_ph0, float32_t exc_f0prev,
                uint8_t flushed, uint8_t voiced);
void C1_FeatHook(const int8_t *feat_q);		/* the int8 NPU input */

void C1_Service(void);			/* arming and the paced dump */

extern volatile uint8_t c1_arm;
extern volatile uint8_t c1_auto;

#endif /* C1_CAPTURE_H */

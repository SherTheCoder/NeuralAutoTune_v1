/*
 * lpc.h - LPC analysis, order 18 at 16 kHz, one call per 5 ms hop
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LPC_H
#define LPC_H

#include "arm_math.h"

#define LPC_ORDER 18u
#define LPC_WIN   320u  /* 20 ms */

void LPC_Init(void);

/* xp320: the newest 320 pre-emphasized samples (windowed internally).
 * k_out: reflection coefficients for the synthesis lattice.
 * a_out: [1, a_1 .. a_18] with x_hat[n] = sum a_m x[n-m], for the analysis FIR.
 * Silence gives k = 0 and a = [1, 0, ...]. */
void LPC_Analyze(const float32_t *xp320, float32_t *k_out, float32_t *a_out);

#endif /* LPC_H */

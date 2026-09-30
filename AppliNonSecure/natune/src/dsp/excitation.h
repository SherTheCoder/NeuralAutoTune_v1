/*
 * excitation.h - band-limited (polyBLEP) sawtooth at the corrected pitch
 *
 * Phase runs continuously across hops while voiced and f0 is ramped linearly
 * over each hop. The output is scaled by the saw's analytic RMS; scaling by
 * each hop's measured RMS puts an amplitude step on every hop and shows up
 * as 200 Hz sidebands.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EXCITATION_H
#define EXCITATION_H

#include <stdint.h>
#include "arm_math.h"

#define EXC_HOP16   80u        /* 5 ms at 16 kHz */
#define EXC_FS16    16000.0f

void Exc_Init(void);

/* only on an unvoiced -> voiced transition */
void Exc_Reset(float32_t f0_hz);

/* one hop, f0 ramped from the previous hop's value */
void Exc_Hop(float32_t f0_hz, float32_t *out);


float32_t Exc_Phase(void);
float32_t Exc_F0Prev(void);

#endif /* EXCITATION_H */

/*
 * spectrum.h - input ring, 48 -> 16 kHz decimation, and a 256-point FFT
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>
#include "arm_math.h"

#define SPECTRUM_BLOCK_SAMPLES 48u

void Spectrum_Init(void);

/* capture interrupt; seq is the DMA block number */
void Spectrum_PushBlock(const int32_t *mic, uint32_t seq);

/* audio task: drain the ring into the voice path and the pitch tracker */
void Spectrum_Pump(void);

const float32_t *Spectrum_DecimCoeffs(uint32_t *ntaps);

void Spectrum_PrintTick(void);

extern volatile uint32_t rb_overruns;

void Spectrum_C1DecimState(float *dst47);

#endif /* SPECTRUM_H */

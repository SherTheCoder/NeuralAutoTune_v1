/*
 * excitation.c - polyBLEP excitation oscillator
 *
 * Must match tools/rsn_v3/excitation/excitation_twin.py, which builds the
 * RSN's training input.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "excitation.h"

#include <math.h>

/* a sawtooth's RMS is 1/sqrt(3) */
#define EXC_RMS_SCALE   1.7320508f

static float32_t s_phase;     /* [0,1) */
static float32_t s_f0_prev;

void Exc_Init(void)
{
    s_phase = 0.0f;
    s_f0_prev = 0.0f;
}

void Exc_Reset(float32_t f0_hz)
{
    s_phase = 0.0f;
    s_f0_prev = f0_hz;
}

/* t: phase in [0,1), dt: phase increment per sample */
static inline float32_t poly_blep(float32_t t, float32_t dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

void Exc_Hop(float32_t f0_hz, float32_t *out)
{
    if (s_f0_prev <= 0.0f) {
        s_f0_prev = f0_hz;
    }

    const float32_t f0_0 = s_f0_prev;
    const float32_t df = (f0_hz - f0_0) / (float32_t)EXC_HOP16;

    for (uint32_t n = 0u; n < EXC_HOP16; n++) {
        const float32_t f = f0_0 + df * (float32_t)(n + 1u);
        const float32_t dt = f * (1.0f / EXC_FS16);

        s_phase += dt;
        s_phase -= floorf(s_phase);

        out[n] = (2.0f * s_phase - 1.0f - poly_blep(s_phase, dt)) * EXC_RMS_SCALE;
    }

    s_f0_prev = f0_hz;
}

float32_t Exc_Phase(void)  { return s_phase; }
float32_t Exc_F0Prev(void) { return s_f0_prev; }

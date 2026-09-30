/*
 * swiftf0_frontend.c - features for the pitch network
 *
 * wave/32768 * Hann -> 1024-point FFT -> magnitude -> triangular projection
 * onto a 20-cent grid -> log2, minus the frame maximum, floored at -16,
 * scaled to [-1, 1]. Matches frontend.py.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "swiftf0.h"

#include <math.h>
#include "arm_math.h"

static arm_rfft_fast_instance_f32 s_rfft;
static uint8_t s_ready;

/* rfft output: [DC, Nyquist, re1, im1, ...] */
static float s_work[SF0_NFFT];
static float s_spec_cplx[SF0_NFFT];

void sf0_frontend_init(void)
{
    if (arm_rfft_fast_init_1024_f32(&s_rfft) == ARM_MATH_SUCCESS) {
        s_ready = 1u;
    }
}

void sf0_features(const int16_t *wave, float *feat)
{
    if (!s_ready) {
        sf0_frontend_init();
    }

    for (uint32_t i = 0u; i < SF0_NFFT; i++) {
        s_work[i] = ((float)wave[i] * (1.0f / 32768.0f)) * SF0_HANN[i];
    }

    arm_rfft_fast_f32(&s_rfft, s_work, s_spec_cplx, 0);

    /* magnitudes, reusing s_work */
    float *spec = s_work;
    spec[0] = fabsf(s_spec_cplx[0]);
    spec[SF0_NSPEC - 1u] = fabsf(s_spec_cplx[1]);
    for (uint32_t k = 1u; k < SF0_NSPEC - 1u; k++) {
        const float re = s_spec_cplx[2u * k];
        const float im = s_spec_cplx[2u * k + 1u];
        spec[k] = sqrtf(re * re + im * im);
    }

    /* projection (sparse rows) and log2 */
    const float *w = SF0_PROJ_W;
    float lg_max = -INFINITY;
    for (uint32_t k = 0u; k < SF0_NBINS_IN; k++) {
        const uint32_t start = SF0_PROJ_START[k];
        const uint32_t cnt = SF0_PROJ_CNT[k];
        float acc = 0.0f;
        for (uint32_t j = 0u; j < cnt; j++) {
            acc += w[j] * spec[start + j];
        }
        w += cnt;
        const float lg = log2f(acc + SF0_LOG_EPS);
        feat[k] = lg;
        if (lg > lg_max) {
            lg_max = lg;
        }
    }

    for (uint32_t k = 0u; k < SF0_NBINS_IN; k++) {
        float lg = feat[k] - lg_max;
        if (lg < SF0_LOG_FLOOR) {
            lg = SF0_LOG_FLOOR;
        }
        feat[k] = lg * (1.0f / 8.0f) + 1.0f;
    }
}

float sf0_bin_to_f0(float b)
{
    return SF0_FMIN * exp2f(b / 60.0f);
}

float sf0_f0_to_bin(float f0)
{
    return 60.0f * log2f(f0 / SF0_FMIN);
}

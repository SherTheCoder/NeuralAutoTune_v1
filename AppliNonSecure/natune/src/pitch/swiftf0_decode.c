/*
 * swiftf0_decode.c - pitch from the network's 360 logits
 *
 * b_hat = weighted mean bin over argmax +-2, f0 = 32.70 * 2^(b_hat/60).
 * The local average removes the 20-cent steps of a plain argmax. Matches
 * decode.py.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "swiftf0.h"

#include <math.h>

float sf0_decode_v(const float *logits, float *b_hat_out, float *entropy_out,
                   float *voicing_out)
{
    float m = logits[0];
    uint32_t amax = 0u;
    for (uint32_t i = 1u; i < SF0_NBINS_OUT; i++) {
        if (logits[i] > m) {
            m = logits[i];
            amax = i;
        }
    }

    /* unnormalized softmax; H = ln(psum) - sum(e*z)/psum, so the
     * probabilities never need storing */
    float psum = 0.0f;
    float e_dot_z = 0.0f;
    for (uint32_t i = 0u; i < SF0_NBINS_OUT; i++) {
        const float z = logits[i] - m;
        const float e = expf(z);
        psum += e;
        e_dot_z += e * z;
    }

    /* argmax +-2, clamped like np.clip */
    float num = 0.0f, den = 0.0f;
    for (int32_t d = -2; d <= 2; d++) {
        int32_t idx = (int32_t)amax + d;
        if (idx < 0) {
            idx = 0;
        } else if (idx >= (int32_t)SF0_NBINS_OUT) {
            idx = (int32_t)SF0_NBINS_OUT - 1;
        }
        const float p = expf(logits[idx] - m);
        num += p * (float)idx;
        den += p;
    }
    const float b_hat = num / den;

    if (b_hat_out) {
        *b_hat_out = b_hat;
    }
    if (entropy_out) {
        const float H = logf(psum) - e_dot_z / psum;
        *entropy_out = H / logf((float)SF0_NBINS_OUT);
    }

    if (voicing_out) {
        /* probability mass near the peak */
        float wmass = 0.0f;
        for (int32_t d = -SF0_VOICE_WIN; d <= SF0_VOICE_WIN; d++) {
            int32_t idx = (int32_t)amax + d;
            if (idx < 0 || idx >= (int32_t)SF0_NBINS_OUT) {
                continue;
            }
            wmass += expf(logits[idx] - m);
        }
        *voicing_out = wmass / psum;
    }

    return sf0_bin_to_f0(b_hat);
}

float sf0_decode(const float *logits, float *b_hat_out, float *entropy_out)
{
    return sf0_decode_v(logits, b_hat_out, entropy_out, NULL);
}

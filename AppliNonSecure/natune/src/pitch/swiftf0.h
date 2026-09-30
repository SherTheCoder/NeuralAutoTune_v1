/*
 * swiftf0.h - pitch tracker front-end and decoder (CPU side)
 *
 * C versions of tools/swiftf0/swiftf0/frontend.py and decode.py; the Python
 * is the reference and tools/swiftf0/ctest checks the two agree.
 *
 * int16 wave[1024] -> features[480] in [-1, 1] -> NPU -> 360 pitch logits
 * + 1 voicing logit -> pitch in Hz
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SWIFTF0_H
#define SWIFTF0_H

#include <stdint.h>
#include "swiftf0_tables.h"

#define SF0_FMIN         32.70f   /* Hz at bin 0 */
#define SF0_LOG_EPS      1e-6f
#define SF0_LOG_FLOOR    (-16.0f)

/* wave: newest 1024 samples at 16 kHz, oldest first. Not reentrant. */
void  sf0_features(const int16_t *wave, float *feat);
void  sf0_frontend_init(void);

/* Returns the pitch in Hz. b_hat is the fractional bin, entropy the
 * normalized entropy of the softmax (0..1). Either may be NULL. */
float sf0_decode(const float *logits, float *b_hat, float *entropy);

/* also returns the probability mass within +-15 bins (+-300 cents) of the
 * peak, a second voicing measure */
#define SF0_VOICE_WIN  15

float sf0_decode_v(const float *logits, float *b_hat, float *entropy, float *voicing);

/* f = 32.70 * 2^(b/60) */
float sf0_bin_to_f0(float b);
float sf0_f0_to_bin(float f0);

#endif /* SWIFTF0_H */

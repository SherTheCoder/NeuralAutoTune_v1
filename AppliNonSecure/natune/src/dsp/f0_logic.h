/*
 * f0_logic.h - target pitch, once per 5 ms hop
 *
 *   onset    jump > 80 cents in one hop: snap to the new note for 3 hops
 *   vibrato  4-8 Hz and > 20 cents deep: keep the vibrato, centred on the note
 *   normal   smoothed pitch (alpha 0.3 per hop) snapped to the scale
 *
 * Vibrato is detected with a 256-point FFT over the last 64 hops of pitch
 * (in cents), every 50 ms.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef F0_LOGIC_H
#define F0_LOGIC_H

#include <stdint.h>
#include "arm_math.h"

#define F0_HIST_HOPS    64u    /* 320 ms */
#define F0_VIB_FFT      256u   /* 0.78 Hz per bin */
#define F0_VIB_PERIOD   10u    /* hops */
#define F0_ONSET_CENTS  80.0f
#define F0_ONSET_HOLD   3u
#define F0_EMA_ALPHA    0.3f
#define F0_VIB_CENTS    20.0f
#define F0_VIB_BIN_LO   5u     /* 3.9 Hz */
#define F0_VIB_BIN_HI   10u    /* 7.8 Hz */

typedef struct {
    float32_t f0_auth;      /* tracked pitch, Hz */
    float32_t f0_smooth;
    float32_t f0_corr;      /* target pitch, Hz */
    uint8_t   onset;
    uint8_t   vibrato;
    float32_t vib_cents;
    float32_t vib_rate_hz;
} f0_state_t;

void F0_Init(void);

/* on unvoiced -> voiced, together with Exc_Reset() */
void F0_Reset(void);

/* returns the target pitch; while unvoiced it returns the last one */
float32_t F0_Update(float32_t f0_auth, uint8_t voiced);

void F0_Get(f0_state_t *out);

extern volatile uint8_t  f0_key_root;         /* 0 = C .. 11 = B */
extern volatile uint16_t f0_scale_mask;       /* bit i: semitone i above the root */
extern volatile uint8_t  f0_vibrato_ratio;    /* 1: f0_auth * snap(m) / m */
extern volatile uint8_t  f0_vibrato_snap_mean;/* with ratio 0: hold snap(m) or m */
extern volatile uint8_t  f0_bypass;           /* no correction */

/* nearest in-scale semitone, A4 = 440 Hz */
float32_t F0_SemitoneSnap(float32_t f0_hz);

#endif /* F0_LOGIC_H */

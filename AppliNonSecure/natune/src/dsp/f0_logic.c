/*
 * f0_logic.c - target pitch (see f0_logic.h)
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "f0_logic.h"

#include <math.h>
#include <string.h>

volatile uint8_t  f0_key_root          = 0u;
volatile uint16_t f0_scale_mask        = 0x0FFFu;
volatile uint8_t  f0_vibrato_snap_mean = 0u;
volatile uint8_t  f0_vibrato_ratio     = 1u;
volatile uint8_t  f0_bypass            = 0u;

/* pitch history in cents, so the vibrato depth threshold means the same at
 * every note */
#define F0_REF   440.0f
static float32_t s_hist[F0_HIST_HOPS];
static uint32_t  s_w;
static uint32_t  s_filled;
static uint32_t  s_since_vib;

static float32_t s_f0_smooth;
static float32_t s_f0_prev_auth;
static float32_t s_f0_corr;
static uint32_t  s_onset_hold;
static uint8_t   s_vibrato;
static float32_t s_vib_cents, s_vib_rate;

static arm_rfft_fast_instance_f32 s_vib_fft;
static float32_t s_vib_win[F0_HIST_HOPS];

static inline float32_t hz_to_cents(float32_t hz)
{
    return 1200.0f * log2f(hz / F0_REF);
}

static inline float32_t cents_to_hz(float32_t c)
{
    return F0_REF * powf(2.0f, c / 1200.0f);
}

void F0_Init(void)
{
    /* the generic init pulls in every twiddle table (~60 KB) */
    (void)arm_rfft_fast_init_256_f32(&s_vib_fft);
    for (uint32_t i = 0u; i < F0_HIST_HOPS; i++) {
        s_vib_win[i] = 0.5f - 0.5f * cosf(2.0f * PI * (float32_t)i /
                                          (float32_t)F0_HIST_HOPS);
    }
    F0_Reset();
}

void F0_Reset(void)
{
    memset(s_hist, 0, sizeof s_hist);
    s_w = 0u;
    s_filled = 0u;
    s_since_vib = 0u;
    s_f0_smooth = 0.0f;
    s_f0_prev_auth = 0.0f;
    s_f0_corr = 0.0f;
    s_onset_hold = 0u;
    s_vibrato = 0u;
    s_vib_cents = 0.0f;
    s_vib_rate = 0.0f;
}

float32_t F0_SemitoneSnap(float32_t f0_hz)
{
    if (f0_hz <= 0.0f) {
        return 0.0f;
    }
    const float32_t st = 12.0f * log2f(f0_hz / F0_REF);
    const int32_t   n0 = (int32_t)lroundf(st);

    const uint16_t mask = f0_scale_mask ? f0_scale_mask : 0x0FFFu;
    int32_t best = n0;
    float32_t best_d = 1e9f;
    for (int32_t d = -6; d <= 6; d++) {
        /* A4 is MIDI 69 */
        const int32_t n = n0 + d;
        const int32_t midi = 69 + n;
        int32_t deg = (midi - (int32_t)f0_key_root) % 12;
        if (deg < 0) {
            deg += 12;
        }
        if ((mask >> deg) & 1u) {
            const float32_t dd = fabsf((float32_t)n - st);
            if (dd < best_d) {
                best_d = dd;
                best = n;
            }
        }
    }
    return F0_REF * powf(2.0f, (float32_t)best / 12.0f);
}

/* de-meaned 64-hop history, Hann, zero-padded 256-point FFT; vibrato if the
 * peak in 3.9-7.8 Hz is deep enough */
static void vibrato_detect(void)
{
    if (s_filled < F0_HIST_HOPS) {
        s_vibrato = 0u;
        s_vib_cents = 0.0f;
        s_vib_rate = 0.0f;
        return;
    }

    static float32_t buf[F0_VIB_FFT];
    static float32_t spec[F0_VIB_FFT];

    float32_t mean = 0.0f;
    for (uint32_t i = 0u; i < F0_HIST_HOPS; i++) {
        mean += s_hist[(s_w + i) % F0_HIST_HOPS];
    }
    mean /= (float32_t)F0_HIST_HOPS;

    for (uint32_t i = 0u; i < F0_HIST_HOPS; i++) {
        buf[i] = (s_hist[(s_w + i) % F0_HIST_HOPS] - mean) * s_vib_win[i];
    }
    memset(&buf[F0_HIST_HOPS], 0, (F0_VIB_FFT - F0_HIST_HOPS) * sizeof(float32_t));

    arm_rfft_fast_f32(&s_vib_fft, buf, spec, 0);

    /* CMSIS packs [DC, Nyquist, re1, im1, ...]. A Hann-windowed sine of
     * amplitude A over N samples peaks at A*N/4. */
    float32_t best_mag = 0.0f;
    uint32_t  best_bin = 0u;
    for (uint32_t k = F0_VIB_BIN_LO; k <= F0_VIB_BIN_HI; k++) {
        const float32_t re = spec[2u * k];
        const float32_t im = spec[2u * k + 1u];
        const float32_t m = sqrtf(re * re + im * im);
        if (m > best_mag) {
            best_mag = m;
            best_bin = k;
        }
    }

    const float32_t amp_cents = 4.0f * best_mag / (float32_t)F0_HIST_HOPS;
    if (amp_cents > F0_VIB_CENTS) {
        s_vibrato = 1u;
        s_vib_cents = amp_cents;
        s_vib_rate = (float32_t)best_bin * (16000.0f / 80.0f) / (float32_t)F0_VIB_FFT;
    } else {
        s_vibrato = 0u;
        s_vib_cents = 0.0f;
        s_vib_rate = 0.0f;
    }
}

static float32_t hist_mean_hz(void)
{
    if (s_filled == 0u) {
        return s_f0_smooth;
    }
    const uint32_t n = (s_filled < F0_HIST_HOPS) ? s_filled : F0_HIST_HOPS;
    float32_t sum = 0.0f;
    for (uint32_t i = 0u; i < n; i++) {
        sum += s_hist[(s_w + (F0_HIST_HOPS - n) + i) % F0_HIST_HOPS];
    }
    return cents_to_hz(sum / (float32_t)n);
}

float32_t F0_Update(float32_t f0_auth, uint8_t voiced)
{
    if (!voiced || f0_auth <= 0.0f) {
        return s_f0_corr;
    }

    if (s_f0_smooth <= 0.0f) {
        s_f0_smooth = f0_auth;
    } else {
        const float32_t d_cents = fabsf(1200.0f * log2f(f0_auth / s_f0_prev_auth));
        if (d_cents > F0_ONSET_CENTS) {
            s_onset_hold = F0_ONSET_HOLD;
            s_f0_smooth = f0_auth;                   /* new note */
            s_filled = 0u;
            s_w = 0u;
            s_vibrato = 0u;
        } else {
            s_f0_smooth = F0_EMA_ALPHA * f0_auth +
                          (1.0f - F0_EMA_ALPHA) * s_f0_smooth;
        }
    }
    s_f0_prev_auth = f0_auth;

    s_hist[s_w] = hz_to_cents(s_f0_smooth);
    s_w = (s_w + 1u) % F0_HIST_HOPS;
    if (s_filled < F0_HIST_HOPS) {
        s_filled++;
    }
    if (++s_since_vib >= F0_VIB_PERIOD) {
        s_since_vib = 0u;
        vibrato_detect();
    }

    if (f0_bypass) {
        s_f0_corr = f0_auth;
    } else if (s_onset_hold > 0u) {
        s_f0_corr = F0_SemitoneSnap(f0_auth);
        s_onset_hold--;
    } else if (s_vibrato) {
        const float32_t m = hist_mean_hz();
        if (f0_vibrato_ratio) {
            /* keep the vibrato, move its centre onto the note */
            s_f0_corr = f0_auth * (F0_SemitoneSnap(m) / m);
        } else {
            s_f0_corr = f0_vibrato_snap_mean ? F0_SemitoneSnap(m) : m;
        }
    } else {
        s_f0_corr = F0_SemitoneSnap(s_f0_smooth);
    }

    return s_f0_corr;
}

void F0_Get(f0_state_t *out)
{
    if (!out) {
        return;
    }
    out->f0_auth = s_f0_prev_auth;
    out->f0_smooth = s_f0_smooth;
    out->f0_corr = s_f0_corr;
    out->onset = (s_onset_hold > 0u) ? 1u : 0u;
    out->vibrato = s_vibrato;
    out->vib_cents = s_vib_cents;
    out->vib_rate_hz = s_vib_rate;
}

/*
 * gate.c - voicing gate (see gate.h)
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gate.h"

#include <string.h>

volatile float32_t gate_v_lo         = 0.5f;
volatile float32_t gate_v_hi         = 0.8f;
volatile uint32_t  gate_zcr_enter    = 90u;
volatile uint32_t  gate_zcr_exit     = 140u;
/* Energy is averaged over the window with two thresholds. Per-block energy
 * dips between glottal pulses and made the gate chatter. */
volatile float32_t gate_energy_floor = 1.0e-5f;
volatile float32_t gate_energy_open  = 4.0e-5f;
volatile uint8_t   gate_force        = 0u;
/* The voicing head reads ~0.1 on room noise and ~0.99 on voice. Window mass
 * reads ~0.5 on the same noise, so the head is the default. */
volatile uint8_t   gate_voicing_src  = 0u;

static uint16_t s_zc[GATE_WIN_BLOCKS];       /* crossings per block */
static uint32_t s_zc_w;
static uint32_t s_zc_sum;
static uint32_t s_filled;

static float32_t s_e_ring[GATE_WIN_BLOCKS];
static float32_t s_e_sum;
static float32_t s_energy;
static uint8_t   s_loud;
static float32_t s_v_head;
static float32_t s_v_wmass;
static float32_t s_voicing;
static int32_t   s_prev_sign;

static gate_state_t s_state;
static uint8_t s_flag_transition;
static uint8_t s_flag_lpc_flush;
static uint8_t s_zcr_voiced;
static uint32_t s_transitions;

void Gate_Init(void)
{
    memset(s_zc, 0, sizeof s_zc);
    memset(s_e_ring, 0, sizeof s_e_ring);
    s_zc_w = 0u;
    s_zc_sum = 0u;
    s_e_sum = 0.0f;
    s_filled = 0u;
    s_energy = 0.0f;
    s_loud = 0u;
    s_v_head = 0.0f;
    s_v_wmass = 0.0f;
    s_voicing = 0.0f;
    s_prev_sign = 0;
    s_state = GATE_UNVOICED;
    s_flag_transition = 0u;
    s_flag_lpc_flush = 0u;
    s_zcr_voiced = 0u;
    s_transitions = 0u;
}

void Gate_SetVoicing(float32_t head, float32_t wmass)
{
    s_v_head = head;
    s_v_wmass = wmass;
    s_voicing = gate_voicing_src ? wmass : head;
}

gate_state_t Gate_PushBlock(const int32_t *mic, uint32_t n)
{
    uint32_t zc = 0u;
    float32_t e = 0.0f;
    for (uint32_t i = 0u; i < n; i++) {
        const int32_t s = mic[i] >> 8;
        const int32_t sign = (s >= 0) ? 1 : -1;
        if (s_prev_sign != 0 && sign != s_prev_sign) {
            zc++;
        }
        s_prev_sign = sign;
        const float32_t v = (float32_t)s * (1.0f / 32768.0f);
        e += v * v;
    }
    const float32_t e_blk = e / (float32_t)n;

    s_zc_sum -= s_zc[s_zc_w];
    s_zc[s_zc_w] = (uint16_t)zc;
    s_zc_sum += zc;
    s_e_sum -= s_e_ring[s_zc_w];
    s_e_ring[s_zc_w] = e_blk;
    s_e_sum += e_blk;
    s_zc_w = (s_zc_w + 1u) % GATE_WIN_BLOCKS;
    if (s_filled < GATE_WIN_BLOCKS) {
        s_filled++;
    }
    s_energy = s_e_sum / (float32_t)GATE_WIN_BLOCKS;

    if (s_energy > gate_energy_open) {
        s_loud = 1u;
    } else if (s_energy < gate_energy_floor) {
        s_loud = 0u;
    }
    const uint8_t loud = s_loud;
    if (s_filled < GATE_WIN_BLOCKS) {
        s_zcr_voiced = 0u;             /* window not full yet */
    } else if (!loud) {
        s_zcr_voiced = 0u;
    } else if (s_zc_sum < gate_zcr_enter) {
        s_zcr_voiced = 1u;
    } else if (s_zc_sum > gate_zcr_exit) {
        s_zcr_voiced = 0u;
    }
    gate_state_t next = s_state;
    if (gate_force == 1u) {
        next = GATE_VOICED;
    } else if (gate_force == 2u) {
        next = GATE_UNVOICED;
    } else if (s_filled < GATE_WIN_BLOCKS) {
        next = GATE_UNVOICED;
    } else if (!loud) {
        next = GATE_UNVOICED;          /* silence */
    } else if (s_state == GATE_VOICED) {
        if (!s_zcr_voiced && (s_voicing < gate_v_lo)) {
            next = GATE_UNVOICED;
        }
    } else {
        if (loud && (s_zcr_voiced || (s_voicing > gate_v_hi))) {
            next = GATE_VOICED;
        }
    }

    if (next != s_state) {
        s_flag_transition = 1u;
        if (next == GATE_VOICED) {
            s_flag_lpc_flush = 1u;
        }
        s_transitions++;
        s_state = next;
    }
    return s_state;
}

gate_state_t Gate_State(void) { return s_state; }

uint8_t Gate_ConsumeTransition(void)
{
    const uint8_t f = s_flag_transition;
    s_flag_transition = 0u;
    return f;
}

uint8_t Gate_ConsumeLpcFlush(void)
{
    const uint8_t f = s_flag_lpc_flush;
    s_flag_lpc_flush = 0u;
    return f;
}

void Gate_GetDiag(gate_diag_t *out)
{
    if (!out) {
        return;
    }
    out->zcr_win = s_zc_sum;
    out->energy = s_energy;
    out->voicing = s_voicing;
    out->v_head = s_v_head;
    out->v_wmass = s_v_wmass;
    out->zcr_says_voiced = s_zcr_voiced;
    out->state = (uint8_t)s_state;
    out->transitions = s_transitions;
}

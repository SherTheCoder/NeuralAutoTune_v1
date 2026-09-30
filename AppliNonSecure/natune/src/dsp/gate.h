/*
 * gate.h - voicing gate
 *
 * Zero-crossing rate and energy over a 30 ms window, combined with the pitch
 * tracker's voicing score. It only decides what gets played (dry input or
 * the corrected voice); the analysis always runs.
 *
 *   voiced -> unvoiced: ZCR/energy say unvoiced AND voicing < v_lo
 *   unvoiced -> voiced: ZCR/energy say voiced   OR  voicing > v_hi
 *
 * The ZCR thresholds are crossings summed over the window (90 crossings in
 * 30 ms is about 1.5 kHz). Below the energy floor the gate is always
 * unvoiced, otherwise a stale voicing score can hold it open in silence.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef GATE_H
#define GATE_H

#include <stdint.h>
#include "arm_math.h"

#define GATE_BLOCK48    48u
#define GATE_WIN_BLOCKS 30u    /* 30 ms window */
#define GATE_XFADE      48u

typedef enum {
    GATE_UNVOICED = 0,
    GATE_VOICED   = 1
} gate_state_t;

void Gate_Init(void);

/* Latest pitch-tracker voicing, every 10 ms. head is the network's voicing
 * output (default), wmass the probability mass near the decoded pitch. */
void Gate_SetVoicing(float32_t head, float32_t wmass);

/* one 1 ms block of raw mic samples (24-bit in int32) */
gate_state_t Gate_PushBlock(const int32_t *mic, uint32_t n);

gate_state_t Gate_State(void);

/* edge flags, cleared on read. LpcFlush is set on unvoiced -> voiced. */
uint8_t Gate_ConsumeTransition(void);
uint8_t Gate_ConsumeLpcFlush(void);

typedef struct {
    uint32_t  zcr_win;       /* crossings in the window */
    float32_t energy;        /* windowed mean square */
    float32_t voicing;       /* the value actually used */
    float32_t v_head;
    float32_t v_wmass;
    uint8_t   zcr_says_voiced;
    uint8_t   state;
    uint32_t  transitions;
} gate_diag_t;

void Gate_GetDiag(gate_diag_t *out);

/* tuning, writable from the debugger */
extern volatile float32_t gate_v_lo;
extern volatile float32_t gate_v_hi;
extern volatile uint32_t  gate_zcr_enter;     /* window sum below: voiced */
extern volatile uint32_t  gate_zcr_exit;      /* window sum above: unvoiced */
extern volatile float32_t gate_energy_floor;  /* close below */
extern volatile float32_t gate_energy_open;   /* open above */
extern volatile uint8_t   gate_force;         /* 0 auto, 1 voiced, 2 unvoiced */
extern volatile uint8_t   gate_voicing_src;   /* 0 voicing head, 1 window mass */

#endif /* GATE_H */

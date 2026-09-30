/*
 * voice_path.h - LPC analysis, RSN on the NPU, LPC synthesis
 *
 * Voice_PushDecimated runs stage A when a hop is complete, Voice_Service
 * (npu task) runs stage B. Voice_ConsumeBlock must be called for every
 * 1 ms block in every mode; it is the play-out clock.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef VOICE_PATH_H
#define VOICE_PATH_H

#include <stdint.h>
#include "arm_math.h"

#define VOICE_BLOCK48 48u

/* play-out delay in 1 ms blocks; stage A + RSN + stage B must fit in it */
#define VOICE_PLAYOUT_BLOCKS 4u

/* latency of the voice path behind the raw input, samples at 48 kHz:
 * block alignment 192 + decimator 23.5 + interpolator 17.5 + play-out */
#define VOICE_D_PATH48 (233u + 48u * VOICE_PLAYOUT_BLOCKS)

/* Voice_ConsumeBlock results */
#define VOICE_OK      0u
#define VOICE_PREROLL 1u   /* nothing to play yet */
#define VOICE_TIER1   2u   /* late, repeating the last period */
#define VOICE_TIER2   3u   /* late > 10 ms, caller plays the dry input */

void Voice_Init(void);
void Voice_PushDecimated(const float32_t *x, uint32_t n);
void Voice_Service(void);
uint8_t Voice_NpuWanted(void);
uint8_t Voice_ConsumeBlock(int16_t *spk, uint32_t n, uint32_t seq);
void Voice_SetBlockSeq(uint32_t q);
uint8_t Voice_FirstWriteDone(void);

/* returns 1 once after each stage B; a good moment to print */
uint8_t Voice_TakePrintSlot(void);

void Voice_PrintTick(void);
uint32_t Voice_HopCount(void);
const float32_t *Voice_UpCoeffs(uint32_t *ntaps);
float32_t Voice_F0Corr(void);

/* C1 capture: analysis history and pre-emphasis state at a hop boundary */
void Voice_C1Snapshot(float32_t *xp240, float32_t *pre);

extern volatile uint8_t  voice_mode;
extern volatile uint8_t  voice_diag_matched;
extern volatile uint8_t  voice_rsn;
extern volatile uint8_t  voice_agc;
extern volatile float    voice_agc_target;
extern volatile float    voice_makeup_db;
extern volatile uint32_t fifo_underruns;

#endif /* VOICE_PATH_H */

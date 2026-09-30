/*
 * hf_path.h - the band above ~7 kHz
 *
 * The voice path runs at 16 kHz, so everything above ~7 kHz is lost. This
 * keeps a 48 kHz delay line of the raw input and adds that band back to
 * the voice output, delayed to line up with it.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HF_PATH_H
#define HF_PATH_H

#include <stdint.h>

#define HF_BLOCK_SAMPLES 48u

void HF_Init(void);

void HF_ProcessBlock(const int32_t *mic, int16_t *spk, uint32_t n);

/* allow_mix = 0 keeps the filters running without adding anything, for
 * blocks that are already full band (bypass, dry route) */
void HF_ProcessBlockGated(const int32_t *mic, int16_t *spk, uint32_t n,
                          uint8_t allow_mix);

/* per block: HF_Write first, then HF_RawAligned and/or HF_Mix */
void HF_Write(const int32_t *mic, uint32_t n);
void HF_RawAligned(int16_t *dst, uint32_t n);
void HF_Mix(int16_t *spk, uint32_t n, uint8_t allow_mix);

extern volatile uint8_t hf_enabled;
extern volatile int8_t  hf_d3_trim;   /* alignment trim, samples */
extern volatile uint8_t hf_complementary; /* 1: HF = raw - voice-band chain */
extern volatile float   hf_gain;

#endif /* HF_PATH_H */

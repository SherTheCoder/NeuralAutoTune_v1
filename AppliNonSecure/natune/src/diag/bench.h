/*
 * bench.h - serial commands and the on-board test bench
 *
 * The bench can replace the microphone input inside the capture interrupt
 * with a synthetic vowel, a note sequence, noise or an uploaded recording,
 * so everything after it sees the test signal as if it came from the mic.
 * Captures are dumped as "D1C|..." hex lines for tools/board/bench.py.
 *
 * Commands, one per line, answered with "OK ..." or "ERR ...":
 *   src mic | vowel <f0> [dBFS] [vib_cents] [vib_hz] | seq <ms> <f0>... | noise [dBFS]
 *   cap <ms>, click, clipn/L/clipcrc/clip play, rec 1|2|0, line <I|O> <off>
 *   mode rsn|plain|bypass, key 0..11, scale chrom|major|minor|<hex>
 *   vib ratio|snap|raw, hf, hfcomp, agc, agct, makeup, route, trim, vsnap
 *   stat
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>

void Bench_Init(void);
void Bench_FillBlock(int32_t *half, uint32_t n);	/* capture interrupt */
void Bench_Click(int16_t *spk, uint32_t n);
void Bench_Capture(const int32_t *mic, const int16_t *spk, uint32_t n);
void Bench_HopRecord(float f0_corr, float f0_auth);
void Bench_Stream(const int32_t *mic, const int16_t *spk, uint32_t n);
void Bench_Service(void);				/* reads command lines */
void Bench_PrintSlot(void);				/* replies and dumps */

extern volatile uint8_t bench_src;     /* 0 mic, 1 vowel, 2 sequence, 3 clip, 4 noise */

#endif /* BENCH_H */

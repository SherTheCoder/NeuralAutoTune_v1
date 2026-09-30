/*
 * lattice.h - reflection-coefficient lattice filters
 *
 * Lattice_Synth is 1/A(z), used for the output. Lattice_Analysis is A(z) and
 * is only used by the matched-lattice diagnostic; with the same k schedule
 * the two are exact inverses. k can change on every call without touching
 * the state, which is what allows per-subframe interpolation.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LATTICE_H
#define LATTICE_H

#include "arm_math.h"
#include "lpc.h"

typedef struct
{
  float32_t b[LPC_ORDER];   /* backward errors b_i(n-1) */
} LatticeState;

void Lattice_Analysis(const float32_t *k, LatticeState *st,
                      const float32_t *x, float32_t *e, uint32_t n);

void Lattice_Synth(const float32_t *k, LatticeState *st,
                   const float32_t *e, float32_t *y, uint32_t n);

void Lattice_StateReset(LatticeState *st);

/* boot-time check against arm_iir_lattice_f32, and timing of both */
void Lattice_BenchAndVerify(void);

#endif /* LATTICE_H */

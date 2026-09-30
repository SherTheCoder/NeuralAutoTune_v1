/*
 * lattice.c - lattice analysis/synthesis and a boot-time check against CMSIS
 *
 * analysis:  f_0 = x,  f_i = f_{i-1} - k_i b_{i-1}(n-1),  e = f_p
 * synthesis: f_p = e,  f_{i-1} = f_i + k_i b_{i-1}(n-1),  y = f_0
 * b_i(n) = b_{i-1}(n-1) - k_i f_{i-1}(n), updated from the top down so each
 * step reads the old value of the one below.
 *
 * arm_iir_lattice_f32 uses the opposite sign for k and stores it reversed.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lattice.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "stm32n6xx.h"

void Lattice_StateReset(LatticeState *st)
{
  memset(st->b, 0, sizeof st->b);
}

void Lattice_Analysis(const float32_t *k, LatticeState *st,
                      const float32_t *x, float32_t *e, uint32_t n)
{
  float32_t f[LPC_ORDER + 1u];
  float32_t *b = st->b;

  for (uint32_t s = 0; s < n; s++)
  {
    f[0] = x[s];
    for (uint32_t i = 0; i < LPC_ORDER; i++)
    {
      f[i + 1u] = f[i] - k[i] * b[i];
    }
    for (uint32_t i = LPC_ORDER - 1u; i > 0u; i--)
    {
      b[i] = b[i - 1u] - k[i - 1u] * f[i - 1u];
    }
    b[0] = f[0];
    e[s] = f[LPC_ORDER];
  }
}

void Lattice_Synth(const float32_t *k, LatticeState *st,
                   const float32_t *e, float32_t *y, uint32_t n)
{
  float32_t f[LPC_ORDER + 1u];
  float32_t *g = st->b;

  for (uint32_t s = 0; s < n; s++)
  {
    f[LPC_ORDER] = e[s];
    for (int32_t i = (int32_t)LPC_ORDER - 1; i >= 0; i--)
    {
      f[i] = f[i + 1] + k[i] * g[i];
    }
    for (uint32_t i = LPC_ORDER - 1u; i > 0u; i--)
    {
      g[i] = g[i - 1u] - k[i - 1u] * f[i - 1u];
    }
    g[0] = f[0];
    y[s] = f[0];
  }
}

#define BENCH_N     80u
#define BENCH_ITERS 1000u

static uint32_t dwt_cycles(void)
{
  return DWT->CYCCNT;
}

void Lattice_BenchAndVerify(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  static float32_t k[LPC_ORDER];
  for (uint32_t i = 0; i < LPC_ORDER; i++)
  {
    k[i] = ((i & 1u) ? -0.7f : 0.7f) / (float32_t)(i + 1u);
  }
  static float32_t in[BENCH_N], out_custom[BENCH_N], out_cmsis[BENCH_N];
  for (uint32_t i = 0; i < BENCH_N; i++)
  {
    in[i] = sinf(0.37f * (float32_t)i) * 0.5f;
  }

  /* CMSIS: k negated and reversed, ladder v = {0 .. 0, 1} */
  static float32_t k_cmsis[LPC_ORDER];
  static float32_t v_cmsis[LPC_ORDER + 1u];
  static float32_t st_cmsis[LPC_ORDER + BENCH_N];
  for (uint32_t i = 0; i < LPC_ORDER; i++)
  {
    k_cmsis[i] = -k[LPC_ORDER - 1u - i];
  }
  memset(v_cmsis, 0, sizeof v_cmsis);
  v_cmsis[LPC_ORDER] = 1.0f;
  arm_iir_lattice_instance_f32 S;
  arm_iir_lattice_init_f32(&S, LPC_ORDER, k_cmsis, v_cmsis, st_cmsis, BENCH_N);

  LatticeState st;
  Lattice_StateReset(&st);
  Lattice_Synth(k, &st, in, out_custom, BENCH_N);
  arm_iir_lattice_f32(&S, in, out_cmsis, BENCH_N);
  float32_t maxdiff = 0.0f;
  for (uint32_t i = 0; i < BENCH_N; i++)
  {
    float32_t d = fabsf(out_custom[i] - out_cmsis[i]);
    if (d > maxdiff) maxdiff = d;
  }

  uint32_t t0 = dwt_cycles();
  for (uint32_t it = 0; it < BENCH_ITERS; it++)
  {
    Lattice_Synth(k, &st, in, out_custom, BENCH_N);
  }
  uint32_t c_custom = (dwt_cycles() - t0) / BENCH_ITERS;

  t0 = dwt_cycles();
  for (uint32_t it = 0; it < BENCH_ITERS; it++)
  {
    arm_iir_lattice_f32(&S, in, out_cmsis, BENCH_N);
  }
  uint32_t c_cmsis = (dwt_cycles() - t0) / BENCH_ITERS;

  /* nano printf has no %f */
  printf("Lattice bench (o%u, %u smp x%u): custom %lu cyc/hop, CMSIS %lu"
         " cyc/hop, maxdiff %lu e-9 (%s)\r\n",
         (unsigned)LPC_ORDER, (unsigned)BENCH_N, (unsigned)BENCH_ITERS,
         (unsigned long)c_custom, (unsigned long)c_cmsis,
         (unsigned long)(maxdiff * 1e9f),
         (maxdiff < 1e-5f) ? "MATCH: synthesis direction verified"
                           : "MISMATCH -- investigate before trusting");
  printf("Lattice production path: custom (supports per-subframe k natively"
         "; flip to CMSIS only if it wins by >20%%)\r\n");
}

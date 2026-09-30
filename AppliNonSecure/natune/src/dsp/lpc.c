/*
 * lpc.c - Hann window, autocorrelation, Levinson-Durbin
 *
 * Returns both the reflection coefficients (synthesis lattice) and the
 * direct-form predictor (analysis filter) from the same recursion.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lpc.h"

#include <math.h>

static float32_t hann[LPC_WIN];

void LPC_Init(void)
{
  for (uint32_t n = 0; n < LPC_WIN; n++)
  {
    hann[n] = 0.5f - 0.5f * cosf(6.28318530718f * (float32_t)n
                                 / (float32_t)(LPC_WIN - 1u));
  }
}

void LPC_Analyze(const float32_t *xp320, float32_t *k_out, float32_t *a_out)
{
  float32_t w[LPC_WIN];
  float32_t R[LPC_ORDER + 1u];
  float32_t a[LPC_ORDER];      /* internal to the recursion */
  float32_t a_prev[LPC_ORDER];

  for (uint32_t n = 0; n < LPC_WIN; n++)
  {
    w[n] = xp320[n] * hann[n];
  }

  /* autocorrelation, lags 0..18 */
  for (uint32_t i = 0; i <= LPC_ORDER; i++)
  {
    float32_t acc = 0.0f;
    for (uint32_t n = i; n < LPC_WIN; n++)
    {
      acc += w[n] * w[n - i];
    }
    R[i] = acc;
  }

  /* silence: pass through */
  if (R[0] < 1e-12f)
  {
    for (uint32_t i = 0; i < LPC_ORDER; i++) k_out[i] = 0.0f;
    a_out[0] = 1.0f;
    for (uint32_t m = 1u; m <= LPC_ORDER; m++) a_out[m] = 0.0f;
    return;
  }
  R[0] *= 1.0001f; /* slight regularization */

  /* Levinson-Durbin. k is clipped inside the recursion so the higher orders
   * are built from the clipped value. */
  float32_t E = R[0];
  for (uint32_t i = 0; i < LPC_ORDER; i++)
  {
    float32_t acc = R[i + 1u];
    for (uint32_t j = 0; j < i; j++)
    {
      acc -= a[j] * R[i - j];
    }
    float32_t ki = (E > 1e-30f) ? (acc / E) : 0.0f;
    if (ki >  0.99f) ki =  0.99f;
    if (ki < -0.99f) ki = -0.99f;
    k_out[i] = ki;

    for (uint32_t j = 0; j < i; j++) a_prev[j] = a[j];
    a[i] = ki;
    for (uint32_t j = 0; j < i; j++)
    {
      a[j] = a_prev[j] - ki * a_prev[i - 1u - j];
    }
    E *= (1.0f - ki * ki);
  }

  /* a[j] is the coefficient of x[n-1-j] */
  a_out[0] = 1.0f;
  for (uint32_t j = 0; j < LPC_ORDER; j++) a_out[j + 1u] = a[j];
}

/*
 * hf_path.c - the band above ~7 kHz
 *
 * HF = raw(t - D_PATH) - chain(t - (D_PATH - 41)), where chain is a copy of
 * the voice path's 48 -> 16 -> 48 kHz filters run on the raw input. Voice
 * band plus HF then adds back up to the input. A 63-tap high-pass split is
 * kept as an option (hfcomp 0); it overlapped the voice band's skirt and
 * gave a +3.8 dB bump around 7 kHz.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "hf_path.h"

#include <stdio.h>
#include <arm_mve.h>
#include "arm_math.h"
#include "voice_path.h"
#include "spectrum.h"

#define HF_TAPS        63u   /* odd: a type II FIR cannot be a high-pass */

/* Helium arm_fir_f32 needs the coefficients padded to a multiple of 4 and
 * a state buffer of numTaps + 2 * blockSize - 1 (not + blockSize - 1). */
#define HF_COEF_PADDED   ((HF_TAPS + 3u) & ~3u)
#define HF_STATE_SIZE    (HF_TAPS + 2u * HF_BLOCK_SAMPLES - 1u)
#define HF_DELAY_SIZE  512u  /* power of two */
#define HF_DELAY_MASK  (HF_DELAY_SIZE - 1u)

/* Voice path latency D_PATH (VOICE_D_PATH48) = 192 (block alignment)
 * + 23.5 (decimator) + 17.5 (interpolator) + 48 per play-out block,
 * 425 samples with 4 blocks. In bypass the tap is a unit delay. */
#define HF_DELAY_BYPASS  1u
#define HF_HPF_GD        31u                       /* (63-1)/2 */
#define HF_TRIM_MAX      16

_Static_assert((HF_DELAY_SIZE & (HF_DELAY_SIZE - 1u)) == 0u,
               "HF_DELAY_SIZE must be a power of two (index masking)");
_Static_assert(VOICE_D_PATH48 + HF_TRIM_MAX + HF_BLOCK_SAMPLES <= HF_DELAY_SIZE,
               "tap + block must fit the delay line");
_Static_assert(HF_BLOCK_SAMPLES % 4u == 0u && HF_DELAY_SIZE % 4u == 0u,
               "wrap split sizes must be multiples of 4");

volatile uint8_t hf_enabled = 1;    /* mix on/off, the filters always run */
volatile int8_t  hf_d3_trim = 0;
volatile float   hf_gain    = 1.0f;

/* scipy.signal.remez(63, [0, 6300, 7300, 24000], [0, 1], fs=48000),
 * plus one zero of padding */
static const float32_t hf_coeffs[HF_COEF_PADDED] = {
  -8.70666521e-04f, -2.03103273e-02f, -3.18141352e-03f, +1.33193544e-03f,
  +6.30840495e-03f, +6.98390044e-03f, +2.13696689e-03f, -5.24116137e-03f,
  -9.61156048e-03f, -6.95873360e-03f, +1.75586001e-03f, +1.05183987e-02f,
  +1.22667998e-02f, +4.39432022e-03f, -8.41054296e-03f, -1.66778200e-02f,
  -1.29079734e-02f, +2.10292048e-03f, +1.83726467e-02f, +2.29434933e-02f,
  +9.57386002e-03f, -1.49060590e-02f, -3.31292885e-02f, -2.83579690e-02f,
  +2.30323219e-03f, +4.19239214e-02f, +6.04993215e-02f, +3.20337449e-02f,
  -4.78876459e-02f, -1.55272983e-01f, -2.47218001e-01f, +7.16660116e-01f,
  -2.47218001e-01f, -1.55272983e-01f, -4.78876459e-02f, +3.20337449e-02f,
  +6.04993215e-02f, +4.19239214e-02f, +2.30323219e-03f, -2.83579690e-02f,
  -3.31292885e-02f, -1.49060590e-02f, +9.57386002e-03f, +2.29434933e-02f,
  +1.83726467e-02f, +2.10292048e-03f, -1.29079734e-02f, -1.66778200e-02f,
  -8.41054296e-03f, +4.39432022e-03f, +1.22667998e-02f, +1.05183987e-02f,
  +1.75586001e-03f, -6.95873360e-03f, -9.61156048e-03f, -5.24116137e-03f,
  +2.13696689e-03f, +6.98390044e-03f, +6.30840495e-03f, +1.33193544e-03f,
  -3.18141352e-03f, -2.03103273e-02f, -8.70666521e-04f, 0.0f, /* pad */
};
static float32_t hf_fir_state[HF_STATE_SIZE];
static arm_fir_instance_f32 hf_fir;

/* raw input, +/-1.0 at 48 kHz; hf_write counts samples written */
__attribute__((aligned(32))) static float32_t hf_delay_line[HF_DELAY_SIZE];
static uint32_t hf_write = 0;

#define HF_CHAIN_GD  41u                  /* 23.5 + 17.5 */
__attribute__((aligned(32))) static float32_t hf_lp_line[HF_DELAY_SIZE];
static arm_fir_decimate_instance_f32    hf_cdec;
static arm_fir_interpolate_instance_f32 hf_cint;
static float32_t hf_cdec_state[48u + HF_BLOCK_SAMPLES - 1u];
static float32_t hf_cint_state[HF_BLOCK_SAMPLES / 3u + 12u - 1u];
volatile uint8_t hf_complementary = 1u;

/* 24-bit -> float, n a multiple of 4 */
static void ConvertToFloat(const int32_t *src, float32_t *dst, uint32_t n)
{
  for (uint32_t i = 0; i < n; i += 4)
  {
    vstrwq_f32(&dst[i], vmulq_n_f32(vcvtq_f32_s32(vldrwq_s32(&src[i])),
                                    1.0f / 8388608.0f));
  }
}

void HF_ProcessBlock(const int32_t *mic, int16_t *spk, uint32_t n)
{
    HF_ProcessBlockGated(mic, spk, n, 1u);
}

void HF_ProcessBlockGated(const int32_t *mic, int16_t *spk, uint32_t n,
                          uint8_t allow_mix)
{
  HF_Write(mic, n);
  HF_Mix(spk, n, allow_mix);
}

static inline int32_t hf_trim(void)
{
  int32_t t = hf_d3_trim;
  if (t >  HF_TRIM_MAX) t =  HF_TRIM_MAX;
  if (t < -HF_TRIM_MAX) t = -HF_TRIM_MAX;
  return t;
}

void HF_Write(const int32_t *mic, uint32_t n)
{
  uint32_t idx   = hf_write & HF_DELAY_MASK;
  uint32_t first = HF_DELAY_SIZE - idx;
  if (first > n) first = n;
  ConvertToFloat(mic, &hf_delay_line[idx], first);
  ConvertToFloat(&mic[first], &hf_delay_line[0], n - first);

  /* the block can wrap in the 512-sample ring, so filter a linear copy */
  float32_t lin[HF_BLOCK_SAMPLES] = {0}, d16[HF_BLOCK_SAMPLES / 3u], lp[HF_BLOCK_SAMPLES];
  for (uint32_t i = 0; i < n; i++) lin[i] = hf_delay_line[(idx + i) & HF_DELAY_MASK];
  arm_fir_decimate_f32(&hf_cdec, lin, d16, n);
  arm_fir_interpolate_f32(&hf_cint, d16, lp, n / 3u);
  for (uint32_t i = 0; i < n; i++) hf_lp_line[(idx + i) & HF_DELAY_MASK] = lp[i];

  hf_write += n;
}

/* raw input delayed by the voice path latency, for the dry route */
void HF_RawAligned(int16_t *dst, uint32_t n)
{
  const uint32_t delay = (uint32_t)((int32_t)VOICE_D_PATH48 + hf_trim());
  const uint32_t start = hf_write - delay - n;
  for (uint32_t i = 0; i < n; i++)
  {
    dst[i] = (int16_t)__SSAT((int32_t)(hf_delay_line[(start + i) & HF_DELAY_MASK]
                                       * 32768.0f), 16);
  }
}

void HF_Mix(int16_t *spk, uint32_t n, uint8_t allow_mix)
{
  float32_t tap[HF_BLOCK_SAMPLES];
  float32_t hf[HF_BLOCK_SAMPLES];
  const uint32_t delay = voice_mode
      ? (uint32_t)((int32_t)(VOICE_D_PATH48 - HF_HPF_GD) + hf_trim())
      : HF_DELAY_BYPASS;
  const uint32_t start = hf_write - delay - n;
  for (uint32_t i = 0; i < n; i++)
  {
    tap[i] = hf_delay_line[(start + i) & HF_DELAY_MASK];
  }
  arm_fir_f32(&hf_fir, tap, hf, n);
  if (voice_mode && hf_complementary)
  {
    const uint32_t d_raw = (uint32_t)((int32_t)VOICE_D_PATH48 + hf_trim());
    const uint32_t s_raw = hf_write - d_raw - n;
    const uint32_t s_lp  = hf_write - (d_raw - HF_CHAIN_GD) - n;
    for (uint32_t i = 0; i < n; i++)
    {
      hf[i] = hf_delay_line[(s_raw + i) & HF_DELAY_MASK] - hf_lp_line[(s_lp + i) & HF_DELAY_MASK];
    }
  }

  /* 1.0 in the delay line is 32768 output LSB */
  if (hf_enabled && allow_mix)
  {
    float32_t g = hf_gain * 32768.0f;
    for (uint32_t i = 0; i < n; i++)
    {
      spk[i] = (int16_t)__SSAT(spk[i] + (int32_t)(hf[i] * g), 16);
    }
  }
}

void HF_Init(void)
{
  arm_fir_init_f32(&hf_fir, HF_TAPS, hf_coeffs, hf_fir_state,
                   HF_BLOCK_SAMPLES);
  uint32_t nd, nu;
  const float32_t *cd = Spectrum_DecimCoeffs(&nd);
  const float32_t *cu = Voice_UpCoeffs(&nu);
  if (nd != 48u || nu != 36u ||
      arm_fir_decimate_init_f32(&hf_cdec, (uint16_t)nd, 3u, cd, hf_cdec_state, HF_BLOCK_SAMPLES) != ARM_MATH_SUCCESS ||
      arm_fir_interpolate_init_f32(&hf_cint, 3u, (uint16_t)nu, cu, hf_cint_state, HF_BLOCK_SAMPLES / 3u) != ARM_MATH_SUCCESS)
  {
    printf("HF_Init: shadow chain init failed\r\n");
    hf_complementary = 0u;
  }
  printf("HF path: tap delay %u bypass / %u voiced (D_PATH %u = 233 + 48 x %u play-out;"
         " D3 trims), aligned raw %u, HPF %u taps equiripple 7 kHz crossover, %s\r\n",
         (unsigned)HF_DELAY_BYPASS, (unsigned)(VOICE_D_PATH48 - HF_HPF_GD),
         (unsigned)VOICE_D_PATH48, (unsigned)VOICE_PLAYOUT_BLOCKS,
         (unsigned)VOICE_D_PATH48, (unsigned)HF_TAPS, hf_enabled ? "enabled" : "muted");
}

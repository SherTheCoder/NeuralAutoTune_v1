/*
 * spectrum.c - input ring, 48 -> 16 kHz decimation, and a 256-point FFT
 *
 * The capture interrupt copies each 48-sample block into a ring. The audio
 * task drains it through the decimator and feeds the 16 kHz stream to the
 * voice path and the pitch tracker.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "spectrum.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <arm_mve.h>
#include "arm_math.h"
#include "main.h"
#include "voice_path.h"
#include "swiftf0_npu.h"
#include "c1_capture.h"

extern volatile uint32_t audio_overruns;

#define RB_SIZE   1920u  /* 40 ms, a multiple of BLOCK so blocks never wrap */
#define BLOCK     SPECTRUM_BLOCK_SAMPLES
#define DECIM_M   3u
#define FIR_TAPS  48u    /* multiple of 4 and of DECIM_M */
#define FFT_SIZE  256u   /* 62.5 Hz per bin */
#define PRINT_PERIOD_MS 1000u

/* 1: replace the mic with a 4 kHz tone plus a 13 kHz tone. The 13 kHz one
 * would alias to 3 kHz, so bin 48 shows how well the decimator rejects it. */
#define TEST_TONE 0
#define TONE_HZ        4000.0f
#define ALIAS_PROBE_HZ 13000.0f
#define TONE_BIN  64u
#define ALIAS_BIN 48u

_Static_assert(RB_SIZE % BLOCK == 0u,
               "RB_SIZE must be a multiple of BLOCK (blocks must never wrap)");
_Static_assert(BLOCK % DECIM_M == 0u,
               "CMSIS decimator requires blockSize % M == 0");
_Static_assert(FFT_SIZE % (BLOCK / DECIM_M) == 0u,
               "decimated blocks must tile decim_buf exactly");
_Static_assert(BLOCK % 4u == 0u,
               "block size must be a multiple of 4 (Helium convert)");

/* Raw DMA samples; conversion to float happens in Spectrum_Pump. rb_head
 * and rb_tail are free-running counters used only for the fill level.
 * 2^32 is not a multiple of RB_SIZE, so the physical indices are kept
 * separately. */
__attribute__((aligned(32))) int32_t ring_buf[RB_SIZE];
volatile uint32_t rb_head = 0;     /* written by the ISR */
static uint32_t   rb_tail = 0;
static uint32_t   rb_widx = 0;
static uint32_t   rb_ridx = 0;
volatile uint32_t rb_overruns = 0;
static uint32_t   ring_seq[RB_SIZE / BLOCK];   /* DMA block number per slot */

/* Anti-alias low-pass for the decimator:
 * scipy.signal.remez(48, [0, 6500, 9500, 24000], [1, 0], weight=[1, 10],
 * fs=48000). Flat to 6.5 kHz, below -63 dB from 9.5 kHz, which is where
 * content starts folding into the voice band. Group delay 23.5 samples is
 * part of the voice path latency (hf_path.c). */
static const float32_t fir_coeffs[FIR_TAPS] = {
  -6.41059910e-04f, -1.94995082e-04f, +1.34449662e-03f, +3.23004795e-03f,
  +3.20467342e-03f, -1.34167329e-04f, -4.81215143e-03f, -5.98887631e-03f,
  -5.37089545e-04f, +8.16384412e-03f, +1.12326623e-02f, +2.52597763e-03f,
  -1.28121930e-02f, -1.96579938e-02f, -6.49117006e-03f, +1.97746530e-02f,
  +3.42050965e-02f, +1.46817641e-02f, -3.21926236e-02f, -6.55265471e-02f,
  -3.66705981e-02f, +6.90613163e-02f, +2.10423274e-01f, +3.11008791e-01f,
  +3.11008791e-01f, +2.10423274e-01f, +6.90613163e-02f, -3.66705981e-02f,
  -6.55265471e-02f, -3.21926236e-02f, +1.46817641e-02f, +3.42050965e-02f,
  +1.97746530e-02f, -6.49117006e-03f, -1.96579938e-02f, -1.28121930e-02f,
  +2.52597763e-03f, +1.12326623e-02f, +8.16384412e-03f, -5.37089545e-04f,
  -5.98887631e-03f, -4.81215143e-03f, -1.34167329e-04f, +3.20467342e-03f,
  +3.23004795e-03f, +1.34449662e-03f, -1.94995082e-04f, -6.41059910e-04f,
};
static float32_t fir_state[FIR_TAPS + BLOCK - 1];
static arm_fir_decimate_instance_f32 fir_decim;

const float32_t *Spectrum_DecimCoeffs(uint32_t *ntaps) { *ntaps = FIR_TAPS; return fir_coeffs; }

static arm_rfft_fast_instance_f32 rfft;
static float32_t decim_buf[FFT_SIZE];
static uint32_t  decim_fill = 0;
static float32_t fft_work[FFT_SIZE];       /* rfft overwrites its input */
static float32_t fft_cplx[FFT_SIZE];
static float32_t spectrum_magsq[FFT_SIZE / 2];
static uint8_t   spectrum_valid = 0;

#if TEST_TONE
static int32_t tone_block[BLOCK];
#endif

/* 24-bit -> float, n a multiple of 4 */
static void ConvertToFloat(const int32_t *src, float32_t *dst, uint32_t n)
{
  for (uint32_t i = 0; i < n; i += 4)
  {
    vstrwq_f32(&dst[i], vmulq_n_f32(vcvtq_f32_s32(vldrwq_s32(&src[i])),
                                    1.0f / 8388608.0f));
  }
}

/* a full-scale sine has magnitude N/2 */
static int MagSqToDb(float32_t magsq)
{
  return (int)(10.0f *
               log10f(magsq * (4.0f / ((float32_t)FFT_SIZE * (float32_t)FFT_SIZE))
                      + 1e-12f));
}

/* capture interrupt */
void Spectrum_PushBlock(const int32_t *mic, uint32_t seq)
{
  if (rb_head - rb_tail > RB_SIZE - BLOCK)
  {
    rb_overruns++;          /* full: drop the new block */
    return;
  }

#if TEST_TONE
  (void)mic;
  const int32_t *src = tone_block;
#else
  const int32_t *src = mic;
#endif
  memcpy(&ring_buf[rb_widx], src, BLOCK * sizeof(int32_t));
  ring_seq[rb_widx / BLOCK] = seq;
  rb_widx += BLOCK;
  if (rb_widx == RB_SIZE) rb_widx = 0;

  rb_head += BLOCK;         /* publish after the data is in place */
}

void Spectrum_Pump(void)
{
  /* read rb_head once; a block that arrives meanwhile waits for the next pass */
  uint32_t head = rb_head;
  while (head - rb_tail >= BLOCK)
  {
    const uint32_t bseq = ring_seq[rb_ridx / BLOCK];
    float32_t linear[BLOCK];
    ConvertToFloat(&ring_buf[rb_ridx], linear, BLOCK);

    C1_PushRaw48(&ring_buf[rb_ridx], BLOCK);

    rb_ridx += BLOCK;
    if (rb_ridx == RB_SIZE) rb_ridx = 0;
    rb_tail += BLOCK;

    arm_fir_decimate_f32(&fir_decim, linear, &decim_buf[decim_fill], BLOCK);
    decim_fill += BLOCK / DECIM_M;

    /* before Voice_PushDecimated: a finished hop may end the capture */
    C1_PushDec16(&decim_buf[decim_fill - BLOCK / DECIM_M], BLOCK / DECIM_M);

    Voice_SetBlockSeq(bseq);
    Voice_PushDecimated(&decim_buf[decim_fill - BLOCK / DECIM_M],
                        BLOCK / DECIM_M);

    sf0_npu_push(&decim_buf[decim_fill - BLOCK / DECIM_M],
                 BLOCK / DECIM_M);

    if (decim_fill == FFT_SIZE)
    {
      decim_fill = 0;
      memcpy(fft_work, decim_buf, sizeof fft_work);
      arm_rfft_fast_f32(&rfft, fft_work, fft_cplx, 0);
      /* power only; dB is computed for printed rows. Bin 0 holds DC and
       * Nyquist packed together. */
      arm_cmplx_mag_squared_f32(fft_cplx, spectrum_magsq, FFT_SIZE / 2);
      spectrum_valid = 1;
    }
  }
}

/* The decimator's last 47 input samples, oldest first. Only valid between
 * blocks. */
void Spectrum_C1DecimState(float32_t *dst47)
{
  _Static_assert(FIR_TAPS - 1u == 47u, "C1_DECHIST must track FIR_TAPS");
  memcpy(dst47, fir_state, (FIR_TAPS - 1u) * sizeof(float32_t));
}

/* prints one row per call so a call never takes long */
void Spectrum_PrintTick(void)
{
  static uint32_t  next_ms = 2000;
  static int32_t   row = -1;               /* -1: idle */
  static float32_t snap[FFT_SIZE / 2];
  static const char bar[] = "########################################";

  if (row < 0)
  {
    if (!spectrum_valid || (int32_t)(HAL_GetTick() - next_ms) < 0)
    {
      return;
    }
    next_ms = HAL_GetTick() + PRINT_PERIOD_MS;
    memcpy(snap, spectrum_magsq, sizeof snap);

    uint32_t pk = 1;                             /* skip DC */
    for (uint32_t k = 2; k < FFT_SIZE / 2; k++)
    {
      if (snap[k] > snap[pk]) pk = k;
    }
    printf("\r\nFFT256@16kHz | peak %lu Hz %d dB | 4k %d dB |"
           " 13k->3k alias %d dB | ovr %lu/%lu\r\n",
           (unsigned long)(pk * 125u / 2u), MagSqToDb(snap[pk]),
           MagSqToDb(snap[TONE_BIN]), MagSqToDb(snap[ALIAS_BIN]),
           (unsigned long)rb_overruns, (unsigned long)audio_overruns);
    row = 0;
    return;
  }

  /* 250 Hz per row, -80..0 dBFS */
  float32_t m = snap[4 * row];
  for (uint32_t k = 1; k < 4; k++)
  {
    if (snap[4 * row + k] > m) m = snap[4 * row + k];
  }
  int db  = MagSqToDb(m);
  int len = (db + 80) / 2;
  if (len < 0)  len = 0;
  if (len > 40) len = 40;
  printf("%4d Hz %4d |%.*s\r\n", (int)(row * 250), db, len, bar);
  if (++row >= (int32_t)(FFT_SIZE / 8)) row = -1;
}

void Spectrum_Init(void)
{
#if TEST_TONE
  /* both tones fit a whole number of cycles into one block */
  for (uint32_t i = 0; i < BLOCK; i++)
  {
    float32_t t = (float32_t)i / 48000.0f;
    float32_t s = 0.6f * sinf(6.28318531f * TONE_HZ * t)
                + 0.5f * sinf(6.28318531f * ALIAS_PROBE_HZ * t);
    tone_block[i] = (int32_t)(s * 8388608.0f);
  }
#endif

  /* the size-specific FFT init avoids linking every twiddle table */
  if (arm_fir_decimate_init_f32(&fir_decim, FIR_TAPS, DECIM_M, fir_coeffs,
                                fir_state, BLOCK) != ARM_MATH_SUCCESS ||
      arm_rfft_fast_init_256_f32(&rfft) != ARM_MATH_SUCCESS)
  {
    printf("Spectrum_Init: CMSIS-DSP init failed\r\n");
    Error_Handler();
  }
  printf("Spectrum: ring %lu samples, FIR %lu taps /%lu -> 16 kHz, FFT %lu"
         " (test tones: %s)\r\n",
         (unsigned long)RB_SIZE, (unsigned long)FIR_TAPS,
         (unsigned long)DECIM_M, (unsigned long)FFT_SIZE,
         TEST_TONE ? "4 kHz + 13 kHz alias probe" : "off, live mic");
}

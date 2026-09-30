/*
 * voice_path.c - LPC analysis, RSN on the NPU, LPC synthesis
 *
 * Every 80-sample hop (5 ms at 16 kHz):
 *
 * stage A (audio task, when the hop is complete)
 *   target pitch -> excitation, LPC analysis -> residual, and the RSN's int8
 *   input window [4][352]: excitation, residual, log2(f0_in/440)/3,
 *   log2(f0_target/440)/3. The hop is queued for the npu task.
 *
 * stage B (npu task, when the RSN is done)
 *   RSN output -> interpolated-k lattice 1/A(z) -> de-emphasis -> x3
 *   upsample -> output ring slot tagged with the hop number
 *
 * consumer (every 1 ms block)
 *   plays the ring VOICE_PLAYOUT_BLOCKS behind the producer. If the hop is
 *   not there yet, the last pitch period is repeated with a 0.97 decay for
 *   up to 10 ms, then the caller switches to the dry input.
 *
 * The RSN takes 2.9 ms, so a 4 ms play-out delay leaves ~0.9 ms of margin.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "voice_path.h"
#include "gate.h"
#include "f0_logic.h"
#include "excitation.h"
#include "swiftf0_npu.h"
#include "c1_capture.h"
#include "rsn_npu.h"
#include "rsn_meta.h"
#include "npu_service.h"
#include "bench.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "main.h"
#include "stm32n6xx.h"
#include "lpc.h"
#include "lattice.h"

#define HOP16       80u
#define NSUB        4u                  /* k interpolation subframes per hop */
#define SUBLEN      (HOP16 / NSUB)
#define HOP48       (HOP16 * 3u)
#define PREEMPH     0.97f
#define UP_L        3u
#define UP_TAPS     36u
#define UP_PHASELEN (UP_TAPS / UP_L)
#define OUT_SCALE   32768.0f
#define PRINT_PERIOD_MS 1000u

#define RING_HOPS   4u
#define RING48      (RING_HOPS * HOP48)
#define BLOCKS_PER_HOP (HOP48 / VOICE_BLOCK48)

#define JOBQ        4u                   /* hops queued for the NPU */

#define OUT_HIST    1024u                /* one period down to 47 Hz */
#define TIER1_BLOCKS 10u
#define CONCEAL_DECAY 0.97f

/* The RSN input has a fixed int8 scale and was trained on peak-normalized
 * audio, but the mic level depends on how far away the singer is. A slow
 * AGC on voiced hops keeps the residual channel near its training level;
 * the output is divided by the same gain, so the level still follows the
 * singer. */
#define AGC_TARGET_RMS  0.18f
#define AGC_ALPHA       0.025f           /* ~200 ms */
#define AGC_MIN         0.125f
#define AGC_MAX         64.0f

/* Residual = static per-hop FIR A(z) = 1 - sum a_j z^-j. Helium FIR needs
 * the taps padded to a multiple of 4 and 2 * blockSize of extra state. */
#define ANALYSIS_TAPS   (LPC_ORDER + 1u)
#define ANALYSIS_PAD    ((ANALYSIS_TAPS + 3u) & ~3u)
#define ANALYSIS_STATE  (ANALYSIS_TAPS + 2u * HOP16 - 1u)

_Static_assert(HOP16 % NSUB == 0u, "subframes must tile the hop");
_Static_assert(UP_TAPS % UP_L == 0u, "CMSIS interpolator: numTaps % L == 0");
_Static_assert(HOP48 == 5u * VOICE_BLOCK48, "hop must be 5 consumer blocks");
_Static_assert(RSN_HOP == HOP16, "the RSN hop is the voice hop");
_Static_assert(RSN_OUT1 - RSN_OUT0 == HOP16, "the RSN emits one hop per window");
_Static_assert(RSN_WINDOW > HOP16, "window holds history + the new hop");
_Static_assert((OUT_HIST & (OUT_HIST - 1u)) == 0u, "OUT_HIST power of two");
_Static_assert(VOICE_PLAYOUT_BLOCKS + BLOCKS_PER_HOP <= RING_HOPS * BLOCKS_PER_HOP,
               "the ring must hold every hop between write and play-out");

volatile uint8_t  voice_mode         = 1;  /* 0 bypass */
volatile uint8_t  voice_diag_matched = 0;  /* matched-lattice test, no RSN */
volatile uint8_t  voice_rsn          = 1;  /* 0: plain LPC round trip */
volatile uint8_t  voice_agc          = 1;
volatile float    voice_agc_target   = AGC_TARGET_RMS;
volatile float    voice_makeup_db    = 1.5f;  /* the RSN comes out ~1.5 dB quiet */
volatile uint32_t fifo_underruns     = 0;  /* late blocks */

static float32_t xp_hist[LPC_WIN];        /* newest 320 pre-emphasized samples */
static uint32_t  hop_fill = 0;
static float32_t pre_state = 0.0f;
static float32_t k_synth_prev[LPC_ORDER]; /* stage B */
static float32_t k_ana_prev[LPC_ORDER];   /* stage A, diagnostic only */
static LatticeState ana_st, syn_st;
static float32_t de_state = 0.0f;

static float32_t exc_new[HOP16];
static float32_t f0_corr_local = 0.0f;
static float32_t f0_auth_held  = 0.0f;    /* held through unvoiced frames */

float32_t Voice_F0Corr(void) { return f0_corr_local; }

static float32_t analysis_fir_taps[ANALYSIS_PAD];
static float32_t analysis_fir_state[ANALYSIS_STATE];
static arm_fir_instance_f32 analysis_fir;

/* RSN input, int8, NCHW [1,4,1,352] */
static int8_t    q_planes[RSN_CH_IN][RSN_WINDOW];
static float32_t agc_lev = 0.0f;
static float32_t agc_gain = 1.0f;

typedef struct {
  uint32_t  hop;
  uint32_t  due;                          /* DMA block number of its first block */
  uint32_t  t_a;                          /* cycle count at stage A */
  float32_t k_cur[LPC_ORDER];
  float32_t e[HOP16];                     /* source residual (plain mode) */
  float32_t xp[HOP16];                    /* input, for the round-trip SNR */
  float32_t out_gain;                     /* RSN LSB -> residual */
  uint8_t   flush;                        /* reset the synthesis lattice */
  uint8_t   use_rsn;
  int8_t    win[RSN_CH_IN * RSN_WINDOW];
} hop_job_t;

/* stage A writes jq_tail, the npu task writes jq_head */
static hop_job_t jobq[JOBQ];
static volatile uint32_t jq_head, jq_tail;
#define JQ_COUNT() (jq_tail - jq_head)
static volatile uint8_t rsn_running;      /* jobq[head] is on the NPU */
static uint32_t  hop_seq;

/* x3 interpolation low-pass, gain 3:
 * scipy.signal.remez(36, [0, 6500, 9500, 24000], [3, 0], weight=[1, 3], fs=48000) */
static const float32_t up_coeffs[UP_TAPS] = {
  -1.34456836e-02f, -1.53413228e-02f, -4.25993679e-03f, +1.81605415e-02f,
  +3.05260105e-02f, +1.21228916e-02f, -2.96230627e-02f, -5.44453101e-02f,
  -2.37437327e-02f, +5.11938028e-02f, +1.00141545e-01f, +5.09694519e-02f,
  -8.67666701e-02f, -1.93835257e-01f, -1.16785788e-01f, +1.98860482e-01f,
  +6.31206672e-01f, +9.42088422e-01f, +9.42088422e-01f, +6.31206672e-01f,
  +1.98860482e-01f, -1.16785788e-01f, -1.93835257e-01f, -8.67666701e-02f,
  +5.09694519e-02f, +1.00141545e-01f, +5.11938028e-02f, -2.37437327e-02f,
  -5.44453101e-02f, -2.96230627e-02f, +1.21228916e-02f, +3.05260105e-02f,
  +1.81605415e-02f, -4.25993679e-03f, -1.53413228e-02f, -1.34456836e-02f,
};
static float32_t up_state[HOP16 + UP_PHASELEN - 1u];

const float32_t *Voice_UpCoeffs(uint32_t *ntaps) { *ntaps = UP_TAPS; return up_coeffs; }
static arm_fir_interpolate_instance_f32 up_inst;

/* Output ring. The writer stores the payload, then the tag; the reader
 * checks the tag first. A slot holds hop h when its tag is h. */
static int16_t  out_ring[RING48];
static volatile uint32_t ring_tag[RING_HOPS];
static volatile uint32_t ring_due[RING_HOPS];
static volatile uint8_t  fifo_primed = 0;
static uint32_t first_due;

/* Play-out is tied to the DMA block numbers: a hop finished while block q
 * was pumped plays blocks q+D .. q+D+4. A dropped block then costs only that
 * block instead of shifting everything after it. */
static uint32_t cur_blk_seq;
static uint32_t last_seq;

void Voice_SetBlockSeq(uint32_t q) { cur_blk_seq = q; }
static int16_t  out_hist[OUT_HIST];       /* what was played, for concealment */
static uint32_t oh_w;
static uint32_t conceal_run;

/* statistics for the 1 Hz status line */
static float32_t snr_sig = 0.0f, snr_err = 0.0f;
static float32_t res_e0 = 0.0f, res_cross = 0.0f;
static uint32_t  hopA_cyc_max = 0, hopB_cyc_max = 0;
static uint32_t  lat_cyc_max = 0;
static int32_t   margin_us_min = 100000;
static uint32_t  t_consume;
static uint32_t  margin_hist[5];                  /* <0.25 <0.5 <1 <2 >=2 ms */
static uint32_t  n_rsn = 0, n_plain = 0, n_drop = 0;
static volatile uint32_t n_hops_total = 0;
static uint32_t  wait_cyc_max = 0;
static volatile uint8_t print_slot;
static uint32_t  n_tier1 = 0, n_tier2 = 0;
static uint32_t  ch1_sat = 0;

/* CMSIS wants the taps reversed: [-a_18 .. -a_1, 1] */
static void Voice_BuildAnalysisTaps(const float32_t *a)
{
  analysis_fir_taps[LPC_ORDER] = a[0];
  for (uint32_t m = 1u; m <= LPC_ORDER; m++)
  {
    analysis_fir_taps[LPC_ORDER - m] = -a[m];
  }
}

static void Voice_InterpSubframeK(float32_t *k_sub, const float32_t *k_prev,
                                  const float32_t *k_cur, uint32_t s)
{
  float32_t w = (float32_t)(s + 1u) / (float32_t)NSUB;
  for (uint32_t i = 0; i < LPC_ORDER; i++)
  {
    k_sub[i] = k_prev[i] + w * (k_cur[i] - k_prev[i]);
  }
}

static inline int8_t q_in(float32_t v)
{
  /* round half to even, like onnxruntime's QuantizeLinear */
  int32_t q = (int32_t)lrintf(v * (1.0f / RSN_IN_SCALE)) + RSN_IN_ZP;
  return (int8_t)__SSAT(q, 8);
}

static inline float32_t f0_norm(float32_t hz)
{
  return (hz > 0.0f) ? log2f(hz * (1.0f / 440.0f)) * (1.0f / 3.0f) : 0.0f;
}

/* stage B: synthesize one hop and put it in the ring, in hop order */
static void Voice_SynthPublish(hop_job_t *j, const float32_t *src, uint8_t plain)
{
  const uint32_t t0 = DWT->CYCCNT;
  float32_t k_sub[LPC_ORDER];
  float32_t y[HOP16];
  float32_t y48[HOP48];

  if (j->flush)
  {
    /* only the lattice delay line; clearing k or the de-emphasis state
     * causes a gain transient that crackles */
    memset(&syn_st, 0, sizeof syn_st);
  }
  for (uint32_t s = 0; s < NSUB; s++)
  {
    Voice_InterpSubframeK(k_sub, k_synth_prev, j->k_cur, s);
    Lattice_Synth(k_sub, &syn_st, &src[s * SUBLEN], &y[s * SUBLEN], SUBLEN);
  }
  memcpy(k_synth_prev, j->k_cur, sizeof k_synth_prev);

  if (plain)
  {
    /* round-trip SNR, only meaningful without pitch shift */
    for (uint32_t n = 0; n < HOP16; n++)
    {
      float32_t d = y[n] - j->xp[n];
      snr_sig += j->xp[n] * j->xp[n];
      snr_err += d * d;
    }
  }

  for (uint32_t n = 0; n < HOP16; n++)
  {
    de_state = y[n] + PREEMPH * de_state;
    y[n] = de_state;
  }
  arm_fir_interpolate_f32(&up_inst, y, y48, HOP16);

  const uint32_t slot = j->hop % RING_HOPS;
  int16_t *dst = &out_ring[slot * HOP48];
  for (uint32_t n = 0; n < HOP48; n++)
  {
    dst[n] = (int16_t)__SSAT((int32_t)(y48[n] * OUT_SCALE), 16);
  }
  ring_tag[slot] = 0xFFFFFFFFu;
  __DMB();
  ring_due[slot] = j->due;
  __DMB();
  ring_tag[slot] = j->hop;

  /* block `due` plays (due - last_seq) ms after the last consumer call */
  const uint32_t now = DWT->CYCCNT;
  const uint32_t lat = now - j->t_a;
  if (lat > lat_cyc_max) lat_cyc_max = lat;
  const int32_t margin_us = ((int32_t)(j->due - last_seq) * 600000 + (int32_t)(t_consume - now)) / 600;
  if (margin_us < margin_us_min) margin_us_min = margin_us;
  margin_hist[margin_us < 250 ? 0 : margin_us < 500 ? 1 : margin_us < 1000 ? 2 : margin_us < 2000 ? 3 : 4]++;

  const uint32_t dt = DWT->CYCCNT - t0;
  if (dt > hopB_cyc_max) hopB_cyc_max = dt;
  print_slot = 1u;
}

uint8_t Voice_TakePrintSlot(void)
{
  const uint8_t r = print_slot;
  print_slot = 0u;
  return r;
}

static void Voice_JobPop(void)
{
  __DMB();
  jq_head = jq_head + 1u;
}

/* npu task only: retire a finished RSN run, publish plain hops that are
 * next in order, start the next RSN run */
void Voice_Service(void)
{
  if (rsn_running)
  {
    if (!rsn_npu_step())
    {
      return;
    }
    rsn_running = 0u;
    hop_job_t *j = &jobq[jq_head % JOBQ];
    const int8_t *o = rsn_npu_output() + RSN_OUT0;
    float32_t src[HOP16];
    for (uint32_t n = 0; n < HOP16; n++)
    {
      src[n] = (float32_t)((int32_t)o[n] - RSN_OUT_ZP) * j->out_gain;
    }
    Voice_SynthPublish(j, src, 0u);
    n_rsn++;
    n_hops_total++;
    Voice_JobPop();
  }

  while (JQ_COUNT() && !jobq[jq_head % JOBQ].use_rsn)
  {
    hop_job_t *j = &jobq[jq_head % JOBQ];
    Voice_SynthPublish(j, j->e, 1u);
    n_plain++;
    n_hops_total++;
    Voice_JobPop();
  }

  if (JQ_COUNT() && !rsn_running && !sf0_npu_busy())
  {
    hop_job_t *j = &jobq[jq_head % JOBQ];
    const uint32_t w = DWT->CYCCNT - j->t_a;
    if (w > wait_cyc_max) wait_cyc_max = w;
    memcpy(rsn_npu_input(), j->win, sizeof j->win);
    rsn_npu_start();
    rsn_running = 1u;
    (void)rsn_npu_step();
  }
}

/* the pitch tracker waits while RSN work is queued; the two share the
 * activation memory */
uint8_t Voice_NpuWanted(void)
{
  return (uint8_t)(rsn_running || JQ_COUNT());
}

/* stage A */
static void Voice_ProcessHop(void)
{
  const uint32_t t0 = DWT->CYCCNT;

  float32_t k_synth_cur[LPC_ORDER];
  float32_t a_analysis[ANALYSIS_TAPS];
  float32_t k_sub[LPC_ORDER];
  float32_t e[HOP16];

  const float32_t *xp_new = &xp_hist[LPC_WIN - HOP16];

  uint8_t   c1_voiced = 0u, c1_flush = 0u;
  float32_t c1_ph0 = 0.0f, c1_fp0 = 0.0f;
  uint8_t   flush = 0u;

  sf0_result_t r;
  sf0_npu_get(&r);
  const uint8_t voiced = (Gate_State() == GATE_VOICED) ? 1u : 0u;
  if (Gate_ConsumeLpcFlush())
  {
    memset(&ana_st, 0, sizeof ana_st);
    flush = 1u;                           /* synthesis side resets in stage B */
    F0_Reset();
    Exc_Reset(F0_SemitoneSnap(r.f0_hz));
    c1_flush = 1u;
  }
  c1_voiced = voiced;
  c1_ph0 = Exc_Phase();
  c1_fp0 = Exc_F0Prev();

  f0_corr_local = F0_Update(r.f0_hz, voiced);
  Exc_Hop(f0_corr_local, exc_new);
  Bench_HopRecord(f0_corr_local, voiced ? r.f0_hz : 0.0f);

  /* channel 2 holds the last confident pitch, as in training */
  if (r.confidence > 0.5f && r.f0_hz > 0.0f)
  {
    f0_auth_held = r.f0_hz;
  }

  LPC_Analyze(xp_hist, k_synth_cur, a_analysis);

  if (voice_diag_matched)
  {
    for (uint32_t s = 0; s < NSUB; s++)
    {
      Voice_InterpSubframeK(k_sub, k_ana_prev, k_synth_cur, s);
      Lattice_Analysis(k_sub, &ana_st, &xp_new[s * SUBLEN], &e[s * SUBLEN], SUBLEN);
    }
  }
  else
  {
    Voice_BuildAnalysisTaps(a_analysis);
    arm_fir_f32(&analysis_fir, xp_new, e, HOP16);
  }

  memcpy(k_ana_prev, k_synth_cur, sizeof k_ana_prev);

  /* residual energy for the AGC, and lag-1 correlation (whiteness) */
  float32_t e2 = e[0] * e[0];
  for (uint32_t n = 1; n < HOP16; n++)
  {
    e2        += e[n] * e[n];
    res_cross += e[n] * e[n - 1u];
  }
  res_e0 += e2;

  if (voiced && e2 > 0.0f)
  {
    const float32_t rms = sqrtf(e2 * (1.0f / (float32_t)HOP16));
    agc_lev = (agc_lev > 0.0f) ? agc_lev + AGC_ALPHA * (rms - agc_lev) : rms;
  }
  float32_t g = 1.0f;
  if (voice_agc && agc_lev > 0.0f)
  {
    g = voice_agc_target / (RSN_RES_SCALE * agc_lev);
    if (g < AGC_MIN) g = AGC_MIN;
    if (g > AGC_MAX) g = AGC_MAX;
  }
  agc_gain = g;

  /* output trim, only recomputed when it changes */
  static float s_makeup_db = 1e9f, s_makeup = 1.0f;
  if (voice_makeup_db != s_makeup_db)
  {
    s_makeup_db = voice_makeup_db;
    s_makeup = powf(10.0f, s_makeup_db / 20.0f);
  }

  /* slide the RSN window by one hop */
  const float32_t s1 = RSN_RES_SCALE * g;
  const int8_t q2 = q_in(f0_norm(f0_auth_held));
  const int8_t q3 = q_in(f0_norm(f0_corr_local));
  for (uint32_t c = 0; c < RSN_CH_IN; c++)
  {
    memmove(&q_planes[c][0], &q_planes[c][HOP16], RSN_WINDOW - HOP16);
  }
  int8_t *p0 = &q_planes[0][RSN_WINDOW - HOP16];
  int8_t *p1 = &q_planes[1][RSN_WINDOW - HOP16];
  int8_t *p2 = &q_planes[2][RSN_WINDOW - HOP16];
  int8_t *p3 = &q_planes[3][RSN_WINDOW - HOP16];
  for (uint32_t n = 0; n < HOP16; n++)
  {
    p0[n] = q_in(exc_new[n]);
    const int8_t v1 = q_in(e[n] * s1);
    if (v1 == 127 || v1 == -128) ch1_sat++;
    p1[n] = v1;
    p2[n] = q2;
    p3[n] = q3;
  }

  if (JQ_COUNT() < JOBQ)
  {
    hop_job_t *j = &jobq[jq_tail % JOBQ];
    j->hop   = hop_seq;
    j->due   = cur_blk_seq + VOICE_PLAYOUT_BLOCKS;
    j->t_a   = t0;
    j->flush = flush;
    j->use_rsn = (uint8_t)(voice_rsn && !voice_diag_matched && rsn_npu_ready());
    j->out_gain = RSN_OUT_SCALE / s1 * s_makeup;
    memcpy(j->k_cur, k_synth_cur, sizeof j->k_cur);
    memcpy(j->e, e, sizeof j->e);
    memcpy(j->xp, xp_new, sizeof j->xp);
    if (j->use_rsn)
    {
      memcpy(j->win, q_planes, sizeof j->win);
    }
    __DMB();
    jq_tail = jq_tail + 1u;
  }
  else
  {
    n_drop++;          /* queue full, this hop gets concealed */
  }

  if (!fifo_primed)
  {
    first_due = cur_blk_seq + VOICE_PLAYOUT_BLOCKS;
    fifo_primed = 1u;
  }
  hop_seq++;

  C1_HopHook(e, exc_new, f0_corr_local, c1_ph0, c1_fp0, c1_flush, c1_voiced);

  NPU_Kick();

  const uint32_t dt = DWT->CYCCNT - t0;
  if (dt > hopA_cyc_max) hopA_cyc_max = dt;
}

void Voice_PushDecimated(const float32_t *x, uint32_t n)
{
  for (uint32_t i = 0; i < n; i++)
  {
    float32_t xn = x[i];
    xp_hist[LPC_WIN - HOP16 + hop_fill] = xn - PREEMPH * pre_state;
    pre_state = xn;
    if (++hop_fill == HOP16)
    {
      Voice_ProcessHop();
      memmove(xp_hist, &xp_hist[HOP16], (LPC_WIN - HOP16) * sizeof(float32_t));
      hop_fill = 0;
    }
  }
}

uint8_t Voice_ConsumeBlock(int16_t *spk, uint32_t n, uint32_t seq)
{
  uint8_t status;
  t_consume = DWT->CYCCNT;
  last_seq = seq;

  if (!fifo_primed || (int32_t)(seq - first_due) < 0)
  {
    memset(spk, 0, n * sizeof(int16_t));
    status = VOICE_PREROLL;
  }
  else
  {
    /* find the slot that covers this block */
    const int16_t *src = NULL;
    for (uint32_t sl = 0; sl < RING_HOPS; sl++)
    {
      const uint32_t tag = ring_tag[sl];
      __DMB();
      const uint32_t k = seq - ring_due[sl];
      if (tag != 0xFFFFFFFFu && k < BLOCKS_PER_HOP)
      {
        src = &out_ring[sl * HOP48 + k * VOICE_BLOCK48];
        break;
      }
    }
    if (src)
    {
      if (conceal_run)
      {
        /* back from concealment: crossfade into the real signal */
        const uint32_t T0 = oh_w;
        uint32_t T = (f0_corr_local > 47.0f) ? (uint32_t)lrintf(48000.0f / f0_corr_local) : 480u;
        for (uint32_t x = 0; x < n; x++)
        {
          const float32_t w = (float32_t)x / (float32_t)n;
          const float32_t c = CONCEAL_DECAY * (float32_t)out_hist[(T0 + x - T) & (OUT_HIST - 1u)];
          spk[x] = (int16_t)__SSAT((int32_t)((1.0f - w) * c + w * (float32_t)src[x]), 16);
        }
        conceal_run = 0u;
      }
      else
      {
        memcpy(spk, src, n * sizeof(int16_t));
      }
      status = VOICE_OK;
    }
    else
    {
      fifo_underruns++;
      if (conceal_run < TIER1_BLOCKS)
      {
        /* repeat the last pitch period, decaying */
        if (conceal_run == 0u) n_tier1++;
        uint32_t T = (f0_corr_local > 47.0f) ? (uint32_t)lrintf(48000.0f / f0_corr_local) : 480u;
        for (uint32_t x = 0; x < n; x++)
        {
          const float32_t c = CONCEAL_DECAY *
              (float32_t)out_hist[(oh_w + x - T) & (OUT_HIST - 1u)];
          spk[x] = (int16_t)c;
          out_hist[(oh_w + x) & (OUT_HIST - 1u)] = spk[x];
        }
        status = VOICE_TIER1;
      }
      else
      {
        if (conceal_run == TIER1_BLOCKS) n_tier2++;
        memset(spk, 0, n * sizeof(int16_t));     /* caller plays the dry input */
        status = VOICE_TIER2;
      }
      conceal_run++;
    }
  }

  for (uint32_t x = 0; x < n; x++)
  {
    out_hist[(oh_w + x) & (OUT_HIST - 1u)] = spk[x];
  }
  oh_w += n;
  return status;
}

uint8_t Voice_FirstWriteDone(void)
{
  return fifo_primed;
}

void Voice_C1Snapshot(float32_t *xp240, float32_t *pre)
{
  memcpy(xp240, &xp_hist[HOP16], (LPC_WIN - HOP16) * sizeof(float32_t));
  *pre = pre_state;
}

void Voice_PrintTick(void)
{
  static uint32_t next_ms = 3500u;
  if ((int32_t)(HAL_GetTick() - next_ms) < 0)
  {
    return;
  }
  next_ms = HAL_GetTick() + PRINT_PERIOD_MS;

  int32_t snr_db10 = 0;
  if (snr_err > 0.0f && snr_sig > 0.0f)
  {
    snr_db10 = (int32_t)(100.0f * log10f(snr_sig / snr_err));
  }
  int32_t snr_frac = snr_db10 % 10; if (snr_frac < 0) snr_frac = -snr_frac;
  int32_t r1x1k = (res_e0 > 0.0f) ? (int32_t)(1000.0f * res_cross / res_e0) : 0;
  const int32_t agc_db10 = (int32_t)(200.0f * log10f(agc_gain));
  uint32_t npu_mx, pick_mx;
  rsn_npu_timing(&npu_mx, &pick_mx, 1u);

  printf("Voice: %s%s | rsn %lu plain %lu drop %lu | lat max %lu us (D %u ms) margin min %ld us "
         "[<.25 %lu <.5 %lu <1 %lu <2 %lu] | miss %lu (t1 %lu t2 %lu) | agc %ld.%ld dB sat %lu | "
         "A %lu B %lu us wait %lu npu %lu pick %lu sf0 %lu us | r1x1k %ld | SNR %ld.%ld\r\n",
         voice_mode ? (voice_rsn && rsn_npu_ready() ? "RSN" : "PLAIN") : "BYPASS",
         voice_diag_matched ? "/DIAG" : "",
         (unsigned long)n_rsn, (unsigned long)n_plain, (unsigned long)n_drop,
         (unsigned long)(lat_cyc_max / 600u), (unsigned)VOICE_PLAYOUT_BLOCKS,
         (long)margin_us_min, (unsigned long)margin_hist[0], (unsigned long)margin_hist[1],
         (unsigned long)margin_hist[2], (unsigned long)margin_hist[3],
         (unsigned long)fifo_underruns, (unsigned long)n_tier1, (unsigned long)n_tier2,
         (long)(agc_db10 / 10), (long)((agc_db10 < 0 ? -agc_db10 : agc_db10) % 10),
         (unsigned long)ch1_sat,
         (unsigned long)(hopA_cyc_max / 600u), (unsigned long)(hopB_cyc_max / 600u),
         (unsigned long)(wait_cyc_max / 600u), (unsigned long)(npu_mx / 600u),
         (unsigned long)(pick_mx / 600u), (unsigned long)(sf0_npu_wall_max(1) / 600u),
         (long)r1x1k, (long)(snr_db10 / 10), (long)snr_frac);
  snr_sig = 0.0f; snr_err = 0.0f;
  res_e0 = 0.0f;  res_cross = 0.0f;
  n_rsn = n_plain = 0u;
  lat_cyc_max = 0u; margin_us_min = 100000;
  hopA_cyc_max = hopB_cyc_max = wait_cyc_max = 0u;
  ch1_sat = 0u;
}

void Voice_Init(void)
{
  LPC_Init();
  Lattice_StateReset(&ana_st);
  Lattice_StateReset(&syn_st);
  memset(k_synth_prev, 0, sizeof k_synth_prev);
  memset(k_ana_prev, 0, sizeof k_ana_prev);
  arm_fir_init_f32(&analysis_fir, ANALYSIS_TAPS, analysis_fir_taps,
                   analysis_fir_state, HOP16);
  if (arm_fir_interpolate_init_f32(&up_inst, UP_L, UP_TAPS, up_coeffs,
                                   up_state, HOP16) != ARM_MATH_SUCCESS)
  {
    printf("Voice_Init: interpolator init failed\r\n");
    Error_Handler();
  }
  memset(q_planes, RSN_IN_ZP, sizeof q_planes);
  for (uint32_t s = 0; s < RING_HOPS; s++)
  {
    ring_tag[s] = 0xFFFFFFFFu;
  }
  Lattice_BenchAndVerify();   /* also turns on the cycle counter */
  printf("Voice path: LPC o%u, static-a analysis FIR + interpolated-k synthesis (%ux%u), "
         "RSN %ux%u int8 -> ring %u hops, play-out %u ms, x%u upsample (mode=%u rsn=%u agc=%u)\r\n",
         (unsigned)LPC_ORDER, (unsigned)NSUB, (unsigned)SUBLEN,
         (unsigned)RSN_CH_IN, (unsigned)RSN_WINDOW, (unsigned)RING_HOPS,
         (unsigned)VOICE_PLAYOUT_BLOCKS, (unsigned)UP_L,
         (unsigned)voice_mode, (unsigned)voice_rsn, (unsigned)voice_agc);
}

uint32_t Voice_HopCount(void)
{
  return n_hops_total;
}

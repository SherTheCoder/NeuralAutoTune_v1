/*
 * swiftf0_npu.c - pitch tracker inference
 *
 * Every 10 ms: newest 1024 samples -> features[480] -> int8 -> NPU ->
 * int8 pitch[360] and voicing[1] -> dequantize -> decode.
 *
 * The quantization constants come from the generated stai_network.h.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "swiftf0_npu.h"
#include "c1_capture.h"
#include "gate.h"
#include "f0_logic.h"
#include "swiftf0.h"
#include "voice_path.h"
#include "npu_service.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

#include "ll_aton_runtime.h"

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(network);
extern const LL_Buffer_InfoTypeDef *LL_ATON_Input_Buffers_Info_network(void);
extern const LL_Buffer_InfoTypeDef *LL_ATON_Output_Buffers_Info_network(void);

/* weights are copied into NPU RAM at init */
extern const uint8_t   sf0_npu_weights[];
extern const uint32_t  sf0_npu_weights_len;
extern const uintptr_t sf0_npu_weights_addr;
/* epoch-controller program (build_sf0_ec.sh); absent in a plain build */
extern const uint8_t   sf0_npu_ecblob[]     __attribute__((weak));
extern const uint32_t  sf0_npu_ecblob_len   __attribute__((weak));
extern const uintptr_t sf0_npu_ecblob_addr  __attribute__((weak));

#define S_IN      0.00784313771873713f
#define Z_IN      (-1)
#define S_PITCH   0.117495745420456f
#define Z_PITCH   16
#define S_VOICE   0.0574029013514519f
#define Z_VOICE   (-90)

static int16_t  s_ring[SF0_NFFT];
static uint32_t s_head;
static uint32_t s_since_infer;
static uint32_t s_primed;            /* ring has been filled once */
static volatile uint32_t s_due;

static int16_t  s_wave[SF0_NFFT];
static float    s_feat[SF0_NBINS_IN];
static float    s_logits[SF0_NBINS_OUT];

static sf0_result_t s_res;
static uint8_t  s_ready;

static volatile uint8_t s_phase;    /* 0 idle, 1 front-end done, 2 on the NPU */
static uint32_t s_t_launch, s_wall_max;
/* The NPU input buffer is in memory shared with the RSN, which may be
 * running, so the input is staged here until launch. */
static int8_t   s_qin[SF0_NBINS_IN];
static uint32_t s_cyc_fe;
static uint32_t s_cyc_npu;
static uint32_t s_max_pass;
static int16_t  s_in_min, s_in_max;

static inline int8_t q_sat(float v)
{
    float q = roundf(v / S_IN) + (float)Z_IN;
    if (q > 127.0f)  q = 127.0f;
    if (q < -128.0f) q = -128.0f;
    return (int8_t)q;
}

void sf0_npu_init(void)
{
    sf0_frontend_init();

    /* copy the weights to NPU RAM and clean the cache so the NPU sees them */
    memcpy((void *)sf0_npu_weights_addr, sf0_npu_weights, sf0_npu_weights_len);
    SCB_CleanDCache_by_Addr((uint32_t *)sf0_npu_weights_addr,
                            (int32_t)sf0_npu_weights_len);
    if (&sf0_npu_ecblob_len != 0 && sf0_npu_ecblob_len != 0u) {
        memcpy((void *)sf0_npu_ecblob_addr, sf0_npu_ecblob, sf0_npu_ecblob_len);
        SCB_CleanDCache_by_Addr((uint32_t *)sf0_npu_ecblob_addr,
                                (int32_t)sf0_npu_ecblob_len);
        printf("SwiftF0: epoch-controller blob %lu B -> 0x%08lX\r\n",
               (unsigned long)sf0_npu_ecblob_len, (unsigned long)sf0_npu_ecblob_addr);
    }

    /* The async runtime lets the CPU sleep while the NPU runs. Without the
     * sleep clock enables the NPU and its RAMs stop in sleep and the first
     * epoch never completes. */
    __HAL_RCC_NPU_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXI_CLK_SLEEP_ENABLE();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM3_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM4_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM5_MEM_CLK_SLEEP_ENABLE();
    __HAL_RCC_AXISRAM6_MEM_CLK_SLEEP_ENABLE();
    printf("NPU: sleep clocks npu %u axisram3-6 %u%u%u%u\r\n",
           (unsigned)(__HAL_RCC_NPU_IS_CLK_SLEEP_ENABLED() != 0),
           (unsigned)(__HAL_RCC_AXISRAM3_MEM_IS_CLK_SLEEP_ENABLED() != 0),
           (unsigned)(__HAL_RCC_AXISRAM4_MEM_IS_CLK_SLEEP_ENABLED() != 0),
           (unsigned)(__HAL_RCC_AXISRAM5_MEM_IS_CLK_SLEEP_ENABLED() != 0),
           (unsigned)(__HAL_RCC_AXISRAM6_MEM_IS_CLK_SLEEP_ENABLED() != 0));

    LL_ATON_RT_RuntimeInit();
    /* RuntimeInit leaves the NPU interrupt at priority 0; move it below the
     * audio DMA */
    HAL_NVIC_SetPriority(NPU0_IRQn, 4, 0);
    LL_ATON_RT_Init_Network(&NN_Instance_network);

    memset(&s_res, 0, sizeof s_res);
    s_head = s_since_infer = s_primed = s_due = 0u;
    s_ready = 1u;
}

/* front-end and quantization; runs on the CPU while the RSN may be on the NPU */
static void sf0_start(void)
{
    const uint32_t t0 = DWT->CYCCNT;

    for (uint32_t i = 0u; i < SF0_NFFT; i++) {
        s_wave[i] = s_ring[(s_head + i) % SF0_NFFT];
    }
    sf0_features(s_wave, s_feat);

    s_in_min = 127; s_in_max = -128;
    for (uint32_t i = 0u; i < SF0_NBINS_IN; i++) {
        int8_t q = q_sat(s_feat[i]);
        s_qin[i] = q;
        if (q < s_in_min) s_in_min = q;
        if (q > s_in_max) s_in_max = q;
    }

    C1_FeatHook(s_qin);

    s_cyc_fe = DWT->CYCCNT - t0;
    s_cyc_npu = 0u;
    if (s_cyc_fe > s_max_pass) { s_max_pass = s_cyc_fe; }
    s_phase = 1u;
}

/* start on the NPU; the caller has checked that the RSN does not need it */
static void sf0_launch(void)
{
    const LL_Buffer_InfoTypeDef *in = LL_ATON_Input_Buffers_Info_network();
    int8_t *in_buf = (int8_t *)LL_Buffer_addr_start(&in[0]);
    memcpy(in_buf, s_qin, SF0_NBINS_IN);
    LL_ATON_Cache_MCU_Clean_Invalidate_Range((uintptr_t)in_buf, SF0_NBINS_IN);
    LL_ATON_RT_Reset_Network(&NN_Instance_network);
    s_t_launch = DWT->CYCCNT;
    s_phase = 2u;
}

uint32_t sf0_npu_wall_max(uint8_t reset)
{
    const uint32_t m = s_wall_max;
    if (reset) { s_wall_max = 0u; }
    return m;
}

/* advance the network; on completion decode and publish */
static void sf0_step(void)
{
    const uint32_t t0 = DWT->CYCCNT;
    /* Async runtime: WFE means the NPU is busy and its interrupt will kick
     * us again, so return instead of waiting. */
    LL_ATON_RT_RetValues_t rt;
    do {
        rt = LL_ATON_RT_RunEpochBlock(&NN_Instance_network);
    } while (rt == LL_ATON_RT_NO_WFE);
    const uint32_t dc = DWT->CYCCNT - t0;
    s_cyc_npu += dc;
    if (dc > s_max_pass) { s_max_pass = dc; }

    if (rt != LL_ATON_RT_DONE) {
        return;
    }
    {
        const uint32_t wall = DWT->CYCCNT - s_t_launch;
        if (wall > s_wall_max) { s_wall_max = wall; }
    }

    const LL_Buffer_InfoTypeDef *out = LL_ATON_Output_Buffers_Info_network();
    const int8_t *voice_q = (const int8_t *)LL_Buffer_addr_start(&out[0]);
    const int8_t *pitch_q = (const int8_t *)LL_Buffer_addr_start(&out[1]);
    LL_ATON_Cache_MCU_Invalidate_Range((uintptr_t)voice_q, 1u);
    LL_ATON_Cache_MCU_Invalidate_Range((uintptr_t)pitch_q, SF0_NBINS_OUT);

    int16_t pmin = 127, pmax = -128;
    for (uint32_t i = 0u; i < SF0_NBINS_OUT; i++) {
        if (pitch_q[i] < pmin) pmin = pitch_q[i];
        if (pitch_q[i] > pmax) pmax = pitch_q[i];
        s_logits[i] = S_PITCH * ((float)pitch_q[i] - (float)Z_PITCH);
    }
    float b_hat, ent, wmass;
    const float f0 = sf0_decode_v(s_logits, &b_hat, &ent, &wmass);
    const float vlogit = S_VOICE * ((float)voice_q[0] - (float)Z_VOICE);

    s_res.f0_hz = f0;
    s_res.confidence = 1.0f / (1.0f + expf(-vlogit));
    s_res.voicing = wmass;
    s_res.entropy = ent;

    Gate_SetVoicing(s_res.confidence, wmass);
    s_res.cyc_fe = s_cyc_fe;
    s_res.cyc_npu = s_cyc_npu;
    s_res.max_pass = s_max_pass;
    s_res.dbg_in_min = s_in_min;
    s_res.dbg_in_max = s_in_max;
    s_res.dbg_pit_min = pmin;
    s_res.dbg_pit_max = pmax;
    s_res.dbg_w_rb  = *(volatile uint8_t *)sf0_npu_weights_addr;
    s_res.dbg_w_exp = sf0_npu_weights[0];
    s_res.count++;
    s_max_pass = 0u;
    s_phase = 0u;
}

void sf0_npu_push(const float *decim16, uint32_t n)
{
    if (!s_ready) {
        return;
    }
    for (uint32_t i = 0u; i < n; i++) {
        float s = decim16[i] * 32768.0f;
        if (s > 32767.0f)  s = 32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        s_ring[s_head] = (int16_t)lrintf(s);
        s_head = (s_head + 1u) % SF0_NFFT;
        if (s_head == 0u) {
            s_primed = 1u;
        }
        if (++s_since_infer >= SF0_HOP_INFER) {
            s_since_infer = 0u;
            if (s_primed) {
                s_due = 1u;
            }
        }
    }
}

void sf0_npu_service(void)
{
    if (!s_ready) {
        return;
    }
    if (s_phase == 0u && s_due) {
        s_due = 0u;          /* requests during an inference are dropped */
        sf0_start();
        NPU_Kick();
    }
}

uint8_t sf0_npu_npu_service(void)
{
    if (!s_ready) {
        return 0u;
    }
    if (s_phase == 1u) {
        if (Voice_NpuWanted()) {
            return 0u;
        }
        sf0_launch();
    }
    if (s_phase == 2u) {
        sf0_step();
        return (uint8_t)(s_phase == 0u);
    }
    return 0u;
}

uint8_t sf0_npu_busy(void)
{
    return (uint8_t)(s_phase == 2u);
}

void sf0_npu_get(sf0_result_t *out)
{
    *out = s_res;
}

void sf0_npu_c1_ring(int16_t *dst1024)
{
    for (uint32_t i = 0u; i < SF0_NFFT; i++) {
        dst1024[i] = s_ring[(s_head + i) % SF0_NFFT];
    }
}

/* nano printf has no %f, so everything is printed as scaled integers */
int sf0_npu_format(char *buf, uint32_t buflen)
{
    int32_t f0_x10    = (int32_t)(s_res.f0_hz * 10.0f + 0.5f);
    /* gate and F0 logic line: vh/vm voicing head and window mass, g/tr gate
     * state and transitions, zcr, f0 -> f0c target, vib depth and rate */
    {
        gate_diag_t gd;
        f0_state_t  fs;
        Gate_GetDiag(&gd);
        F0_Get(&fs);

        /* speech has at most ~15 onsets/s, over 40 means the gate chatters */
        static uint32_t tr_prev = 0u;
        const uint32_t tr_rate = gd.transitions - tr_prev;
        tr_prev = gd.transitions;


        const float e_db  = 10.0f * log10f(gd.energy + 1e-12f);
        const float fl_db = 10.0f * log10f(gate_energy_floor + 1e-12f);
        printf("B3 %s vh %ld.%02ld vm %ld.%02ld | g %s tr %lu (%lu/s)%s | zcr %lu (in<%lu out>%lu) | "
               "e %ld dBFS (floor %ld, %s) | f0 %ld.%ld -> f0c %ld.%ld | "
               "vib %u %ldc %ld.%ldHz%s\r\n",
               gate_voicing_src ? "[wmass]" : "[head] ",
               (long)(gd.v_head), (long)(gd.v_head * 100.0f) % 100,
               (long)(gd.v_wmass), (long)(gd.v_wmass * 100.0f) % 100,
               gd.state ? "V" : "u", (unsigned long)gd.transitions,
               (unsigned long)tr_rate, (tr_rate > 40u) ? " CHATTER!" : "",
               (unsigned long)gd.zcr_win,
               (unsigned long)gate_zcr_enter, (unsigned long)gate_zcr_exit,
               (long)e_db, (long)fl_db,
               (gd.energy > gate_energy_floor) ? "LOUD" : "quiet",
               (long)fs.f0_auth, (long)(fs.f0_auth * 10.0f) % 10,
               (long)fs.f0_corr, (long)(fs.f0_corr * 10.0f) % 10,
               fs.vibrato, (long)fs.vib_cents,
               (long)fs.vib_rate_hz, (long)(fs.vib_rate_hz * 10.0f) % 10,
               fs.onset ? "  ONSET" : "");
    }

    int32_t conf_x100 = (int32_t)(s_res.confidence * 100.0f + 0.5f);
    int32_t ent_x100  = (int32_t)(s_res.entropy * 100.0f + 0.5f);
    return snprintf(buf, buflen,
                    "SwiftF0: %ld.%ld Hz conf %ld.%02ld H %ld.%02ld "
                    "(fe %lu npu %lu maxpass %lu n=%lu) "
                    "in[%d,%d] pit[%d,%d] w=%02x/%02x",
                    (long)(f0_x10 / 10), (long)(f0_x10 % 10),
                    (long)(conf_x100 / 100), (long)(conf_x100 % 100),
                    (long)(ent_x100 / 100), (long)(ent_x100 % 100),
                    (unsigned long)s_res.cyc_fe, (unsigned long)s_res.cyc_npu,
                    (unsigned long)s_res.max_pass, (unsigned long)s_res.count,
                    (int)s_res.dbg_in_min, (int)s_res.dbg_in_max,
                    (int)s_res.dbg_pit_min, (int)s_res.dbg_pit_max,
                    (unsigned)s_res.dbg_w_rb, (unsigned)s_res.dbg_w_exp);
}

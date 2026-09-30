/*
 * rsn_npu.c - the RSN on the Neural-ART (see rsn_npu.h)
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rsn_npu.h"
#include "rsn_meta.h"
#include "rsn_film.h"
#include "rsn_golden.h"
#include "npu_service.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "ll_aton_runtime.h"

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(rsn);

/* -DRSN_CACHE_NEG_TEST=1 removes the cache maintenance; the self-test
 * should then fail, which shows the maintenance is actually needed */
#ifndef RSN_CACHE_NEG_TEST
#define RSN_CACHE_NEG_TEST 0
#endif

/* the polling runtime silently skips epoch-controller blobs */
#if RSN_EC && (LL_ATON_RT_MODE != LL_ATON_RT_ASYNC)
#error "rsn.c is an epoch-controller build: compile with LL_ATON_RT_MODE=2 (ASYNC)"
#endif

#define RSN_IN_BYTES    (RSN_CH_IN * RSN_WINDOW)
#define RSN_FILM_BYTES  96u
#define RSN_OUT_BYTES   RSN_WINDOW
/* The NPU rounds differently from onnxruntime: typically 2-5 LSB max and
 * well under 1 LSB mean. A wrong graph or layout gives ~40 max, ~10 mean. */
#define RSN_TOL_LSB       8
#define RSN_TOL_MEAN_MLSB 1000u

static uint8_t  s_ok;
static uint8_t  s_busy;
static int8_t  *s_in;
static int8_t  *s_film[RSN_N_FILM];
static const int8_t *s_out;
static uintptr_t s_in_lo, s_in_hi;         /* span of all inputs, for cache ops */
static uint32_t s_t0, s_cyc_last, s_cyc_max, s_count;
static volatile uint32_t s_t_irq;
static uint32_t s_npu_max, s_pick_max;

/* called from the NPU interrupt (LL_ATON_OSAL_SIGNAL_EVENT in ll_aton_osal.h) */
void npu_event_hook(void)
{
    s_t_irq = DWT->CYCCNT;
    NPU_Kick();
}

void rsn_npu_timing(uint32_t *npu_max, uint32_t *pickup_max, uint8_t reset)
{
    *npu_max = s_npu_max;
    *pickup_max = s_pick_max;
    if (reset) { s_npu_max = s_pick_max = 0u; }
}

/* graph order: gamma0, delta0, gamma1, delta1, ..., gamma5, delta5 */
static const int8_t *const s_film_q8[RSN_N_FILM] = {
    rsn_gamma0_q8, rsn_delta0_q8, rsn_gamma1_q8, rsn_delta1_q8,
    rsn_gamma2_q8, rsn_delta2_q8, rsn_gamma3_q8, rsn_delta3_q8,
    rsn_gamma4_q8, rsn_delta4_q8, rsn_gamma5_q8, rsn_delta5_q8,
};
static const int8_t *const s_gold_in[RSN_GOLDEN_N] = {
    rsn_golden_in0, rsn_golden_in1, rsn_golden_in2, rsn_golden_in3,
    rsn_golden_in4, rsn_golden_in5, rsn_golden_in6, rsn_golden_in7,
};
static const int8_t *const s_gold_out[RSN_GOLDEN_N] = {
    rsn_golden_out0, rsn_golden_out1, rsn_golden_out2, rsn_golden_out3,
    rsn_golden_out4, rsn_golden_out5, rsn_golden_out6, rsn_golden_out7,
};

/* same as zlib.crc32 */
static uint32_t crc32_ieee(const uint8_t *p, uint32_t n)
{
    static uint32_t tab[256];
    static uint8_t  init;
    if (!init) {
        for (uint32_t i = 0u; i < 256u; i++) {
            uint32_t c = i;
            for (uint32_t k = 0u; k < 8u; k++) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            tab[i] = c;
        }
        init = 1u;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < n; i++) {
        c = tab[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

int8_t *rsn_npu_input(void)          { return s_in; }
uint8_t rsn_npu_ready(void)          { return s_ok; }
uint8_t rsn_npu_busy(void)           { return s_busy; }
const int8_t *rsn_npu_output(void)   { return s_out; }
uint32_t rsn_npu_cycles_last(void)   { return s_cyc_last; }
uint32_t rsn_npu_cycles_max(void)    { return s_cyc_max; }
uint32_t rsn_npu_count(void)         { return s_count; }

void rsn_npu_start(void)
{
    /* the pitch tracker may have overwritten the FiLM inputs */
    for (uint32_t i = 0u; i < RSN_N_FILM; i++) {
        memcpy(s_film[i], s_film_q8[i], RSN_FILM_BYTES);
    }
#if !RSN_CACHE_NEG_TEST
    LL_ATON_Cache_MCU_Clean_Invalidate_Range(s_in_lo, (uint32_t)(s_in_hi - s_in_lo));
#endif
    LL_ATON_RT_Reset_Network(&NN_Instance_rsn);
    s_t0 = DWT->CYCCNT;
    s_busy = 1u;
}

uint8_t rsn_npu_step(void)
{
    if (!s_busy) {
        return 1u;
    }
    /* NO_WFE: more work can be done now; WFE: wait for the interrupt */
    LL_ATON_RT_RetValues_t rt;
    do {
        rt = LL_ATON_RT_RunEpochBlock(&NN_Instance_rsn);
    } while (rt == LL_ATON_RT_NO_WFE);
    if (rt != LL_ATON_RT_DONE) {
        return 0u;
    }
#if !RSN_CACHE_NEG_TEST
    LL_ATON_Cache_MCU_Invalidate_Range((uintptr_t)s_out, RSN_OUT_BYTES);
#endif
    {
        const uint32_t now = DWT->CYCCNT, ti = s_t_irq;
        if ((uint32_t)(ti - s_t0) < (uint32_t)(now - s_t0)) {
            if (ti - s_t0 > s_npu_max) s_npu_max = ti - s_t0;
            if (now - ti > s_pick_max) s_pick_max = now - ti;
        }
    }
    s_cyc_last = DWT->CYCCNT - s_t0;
    if (s_cyc_last > s_cyc_max) { s_cyc_max = s_cyc_last; }
    s_count++;
    s_busy = 0u;
    return 1u;
}

/* self-test only */
static void run_blocking(void)
{
    rsn_npu_start();
    while (!rsn_npu_step()) {
        LL_ATON_OSAL_WFE();
    }
}

uint8_t rsn_npu_init(void)
{
    s_ok = 0u;

    /* the debugger wrote the weights behind the cache */
    SCB_InvalidateDCache_by_Addr((uint32_t *)RSN_WEIGHTS_ADDR, (int32_t)RSN_WEIGHTS_LEN);
    const uint32_t crc = crc32_ieee((const uint8_t *)RSN_WEIGHTS_ADDR, RSN_WEIGHTS_LEN);
    printf("RSN: NPU image (weights%s) @0x%08lX %lu B, CRC32 0x%08lX (want 0x%08lX) %s\r\n",
           RSN_EC ? " + EC blob" : "", (unsigned long)RSN_WEIGHTS_ADDR, (unsigned long)RSN_WEIGHTS_LEN,
           (unsigned long)crc, (unsigned long)RSN_WEIGHTS_CRC32,
           crc == RSN_WEIGHTS_CRC32 ? "OK" : "MISMATCH -- load them with tools/board/board.sh");
    if (crc != RSN_WEIGHTS_CRC32) {
        return 0u;
    }

    /* the real inputs are the entries with is_param == 0 */
    LL_ATON_RT_Init_Network(&NN_Instance_rsn);
    const LL_Buffer_InfoTypeDef *in = LL_ATON_Input_Buffers_Info_rsn();
    uint32_t n_real = 0u;
    for (uint32_t i = 0u; in[i].name != NULL; i++) {
        if (in[i].is_param) { continue; }
        uint8_t *a = (uint8_t *)LL_Buffer_addr_start(&in[i]);
        uint32_t len = (uint32_t)LL_Buffer_len(&in[i]);
        if (n_real == 0u) {
            if (len != RSN_IN_BYTES) { printf("RSN: input0 is %lu B, want %u\r\n", (unsigned long)len, (unsigned)RSN_IN_BYTES); return 0u; }
            s_in = (int8_t *)a;
            s_in_lo = (uintptr_t)a; s_in_hi = (uintptr_t)a + len;
        } else if (n_real <= RSN_N_FILM) {
            if (len != RSN_FILM_BYTES) { printf("RSN: FiLM input %lu is %lu B, want 96\r\n", (unsigned long)n_real, (unsigned long)len); return 0u; }
            s_film[n_real - 1u] = (int8_t *)a;
            if ((uintptr_t)a < s_in_lo) { s_in_lo = (uintptr_t)a; }
            if ((uintptr_t)a + len > s_in_hi) { s_in_hi = (uintptr_t)a + len; }
        }
        n_real++;
    }
    if (n_real != 1u + RSN_N_FILM) {
        printf("RSN: %lu real inputs, want %u\r\n", (unsigned long)n_real, (unsigned)(1u + RSN_N_FILM));
        return 0u;
    }
    const LL_Buffer_InfoTypeDef *out = LL_ATON_Output_Buffers_Info_rsn();
    s_out = (const int8_t *)LL_Buffer_addr_start(&out[0]);
    if ((uint32_t)LL_Buffer_len(&out[0]) != RSN_OUT_BYTES) {
        printf("RSN: output is %lu B, want %u\r\n", (unsigned long)LL_Buffer_len(&out[0]), (unsigned)RSN_OUT_BYTES);
        return 0u;
    }

    /* golden vectors from the host INT8 graph */
    int worst = 0;
    uint32_t n_diff = 0u, sum_d = 0u;
    for (uint32_t k = 0u; k < RSN_GOLDEN_N; k++) {
        memcpy(s_in, s_gold_in[k], RSN_IN_BYTES);
        run_blocking();
        for (uint32_t j = 0u; j < RSN_OUT_BYTES; j++) {
            int d = (int)s_out[j] - (int)s_gold_out[k][j];
            if (d < 0) { d = -d; }
            if (d > worst) { worst = d; }
            if (d) { n_diff++; }
            sum_d += (uint32_t)d;
        }
    }
    /* timing */
    uint32_t cmin = 0xFFFFFFFFu, cmax = 0u;
    uint64_t csum = 0u;
    for (uint32_t it = 0u; it < 50u; it++) {
        memcpy(s_in, s_gold_in[it % RSN_GOLDEN_N], RSN_IN_BYTES);
        run_blocking();
        const uint32_t c = s_cyc_last;
        if (c < cmin) { cmin = c; }
        if (c > cmax) { cmax = c; }
        csum += c;
    }
    const uint32_t mhz = HAL_RCC_GetCpuClockFreq() / 1000000u;
    const uint32_t mean_mlsb = (uint32_t)((1000ull * sum_d) / (RSN_GOLDEN_N * RSN_OUT_BYTES));
    const uint8_t pass = (worst <= RSN_TOL_LSB && mean_mlsb <= RSN_TOL_MEAN_MLSB) ? 1u : 0u;
    printf("RSN: golden %u windows x %u: %lu bytes differ, max |d| %d LSB (tol %d), mean |d| %lu.%03lu LSB (tol 1.000) -> %s\r\n",
           (unsigned)RSN_GOLDEN_N, (unsigned)RSN_OUT_BYTES, (unsigned long)n_diff, worst,
           RSN_TOL_LSB, (unsigned long)(mean_mlsb / 1000u), (unsigned long)(mean_mlsb % 1000u),
           pass ? "PASS" : "FAIL");
    printf("RSN: inference (50) min %lu / mean %lu / max %lu cyc = %lu / %lu / %lu us @ %lu MHz\r\n",
           (unsigned long)cmin, (unsigned long)(csum / 50u), (unsigned long)cmax,
           (unsigned long)(cmin / mhz), (unsigned long)(csum / 50u / mhz),
           (unsigned long)(cmax / mhz), (unsigned long)mhz);
    s_cyc_max = 0u;
    s_count = 0u;
    s_ok = pass;
#if RSN_CACHE_NEG_TEST
    printf("RSN: *** CACHE NEGATIVE TEST BUILD: clean/invalidate removed; RSN kept in the path anyway ***\r\n");
    s_ok = 1u;
#endif
    return s_ok;
}

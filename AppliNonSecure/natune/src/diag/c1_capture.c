/*
 * c1_capture.c - capture and dump (see c1_capture.h)
 *
 * The capture starts at a hop boundary. At that point the analysis history,
 * the pre-emphasis state and the decimator history are copied. The pitch
 * tracker's ring is copied one block later, once it holds the samples up to
 * the boundary. Sample 0 of the 16 kHz stream is the first sample of the
 * first captured hop; raw sample index = 3 * decimated index.
 *
 * Dump lines:
 *   C1|H|k=v;...                   header
 *   C1|<s>|<offset>|<hex>|<crc8>   section data, up to 120 bytes per line
 *   C1|Z|<total>|<crc32>           end
 * Sections: X xp history, D decimator history, G tracker ring, R raw,
 * d 16 kHz, E residual, C excitation, M hop metadata, F features, T frame
 * positions. c1_crossval.py depends on this layout.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "c1_capture.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "console.h"
#include "gate.h"
#include "voice_path.h"
#include "spectrum.h"
#include "swiftf0_npu.h"
#include "swiftf0.h"

extern volatile uint32_t audio_overruns;
extern volatile uint32_t rb_overruns;

volatile uint8_t c1_arm  = 0u;
volatile uint8_t c1_auto = 1u;

typedef enum {
    C1_IDLE = 0,
    C1_ARMED,      /* waiting for a hop boundary */
    C1_PENDING,    /* waiting one block for the tracker ring */
    C1_RUN,
    C1_DUMP,
    C1_DONE
} c1_state_t;

static volatile c1_state_t s_state = C1_IDLE;
static uint8_t  s_auto_done = 0u;         /* auto-arm only once per boot */
static uint32_t s_voiced_ms = 0u;

/* The large buffers go in AXISRAM1 (not zeroed, always written before being
 * read), the rest in normal RAM. Only 224 KB of AXISRAM1 is usable. */
#define C1SEC_AX __attribute__((section(".axisram1"), aligned(32)))

typedef struct {
    float32_t f0_corr;
    float32_t ph0;        /* oscillator phase at hop start */
    float32_t f0prev;
    uint8_t   flags;      /* bit0 voiced, bit1 flushed */
    uint8_t   pad[3];
} c1_hopmeta_t;

C1SEC_AX static int16_t   s_raw[C1_RAW48N];
C1SEC_AX static int16_t   s_dec[C1_DEC16N];
C1SEC_AX static int8_t    s_featbuf[C1_TICKS * SF0_NBINS_IN];
static float32_t          s_res[C1_HOPS * C1_HOP16];
static float32_t          s_exc[C1_HOPS * C1_HOP16];
static c1_hopmeta_t       s_hopm[C1_HOPS];
static uint32_t           s_tickc[C1_TICKS];
static float32_t          s_xp240[C1_XPHIST];
static float32_t          s_dstate[C1_DECHIST];
static int16_t            s_ring0[C1_SF0RING];

static float32_t s_pre;
static uint32_t  s_rawn, s_decn, s_hopn, s_tickn;
static uint32_t  s_ovr0[3];               /* error counters at start */
static uint8_t   s_mode0, s_diag0;
static uint8_t   s_hdr_sent;

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (uint32_t k = 0u; k < 8u; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void C1_PushRaw48(const int32_t *mic, uint32_t n)
{
    if (s_state == C1_PENDING) {
        /* the tracker ring now ends at the boundary */
        sf0_npu_c1_ring(s_ring0);
        s_state = C1_RUN;
        printf("C1: capture started (%u hops = %u ms)\r\n",
               (unsigned)C1_HOPS, (unsigned)(C1_HOPS * 5u));
    }
    if (s_state != C1_RUN || s_rawn + n > C1_RAW48N) {
        return;
    }
    for (uint32_t i = 0u; i < n; i++) {
        int32_t v = mic[i] >> 8;
        if (v > 32767)  v = 32767;
        if (v < -32768) v = -32768;
        s_raw[s_rawn + i] = (int16_t)v;
    }
    s_rawn += n;
}

void C1_PushDec16(const float32_t *dec, uint32_t n)
{
    if (s_state != C1_RUN || s_decn + n > C1_DEC16N) {
        return;
    }
    for (uint32_t i = 0u; i < n; i++) {
        /* same conversion as sf0_npu_push */
        float32_t s = dec[i] * 32768.0f;
        if (s > 32767.0f)  s = 32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        s_dec[s_decn + i] = (int16_t)lrintf(s);
    }
    s_decn += n;
}

void C1_HopHook(const float32_t *e80, const float32_t *exc80,
                float32_t f0_corr, float32_t exc_ph0, float32_t exc_f0prev,
                uint8_t flushed, uint8_t voiced)
{
    if (s_state == C1_ARMED) {
        /* hop boundary, before the window slides */
        Voice_C1Snapshot(s_xp240, &s_pre);
        Spectrum_C1DecimState(s_dstate);
        s_rawn = s_decn = s_hopn = s_tickn = 0u;
        s_ovr0[0] = audio_overruns;
        s_ovr0[1] = rb_overruns;
        s_ovr0[2] = fifo_underruns;
        s_mode0 = voice_mode;
        s_diag0 = voice_diag_matched;
        s_hdr_sent = 0u;
        s_state = C1_PENDING;
        return;                     /* this hop is not captured */
    }
    if (s_state != C1_RUN || s_hopn >= C1_HOPS) {
        return;
    }
    memcpy(&s_res[s_hopn * C1_HOP16], e80,   C1_HOP16 * sizeof(float32_t));
    memcpy(&s_exc[s_hopn * C1_HOP16], exc80, C1_HOP16 * sizeof(float32_t));
    c1_hopmeta_t *m = &s_hopm[s_hopn];
    m->f0_corr = f0_corr;
    m->ph0     = exc_ph0;
    m->f0prev  = exc_f0prev;
    m->flags   = (uint8_t)((voiced ? 1u : 0u) | (flushed ? 2u : 0u));
    m->pad[0] = m->pad[1] = m->pad[2] = 0u;
    if (++s_hopn == C1_HOPS) {
        s_state = C1_DUMP;
    }
}

void C1_FeatHook(const int8_t *feat_q)
{
    /* a frame can land in the pass that started the capture */
    if ((s_state != C1_RUN && s_state != C1_PENDING) || s_tickn >= C1_TICKS) {
        return;
    }
    memcpy(&s_featbuf[s_tickn * SF0_NBINS_IN], feat_q, SF0_NBINS_IN);
    s_tickc[s_tickn] = s_rawn / 3u;   /* 16 kHz samples captured so far */
    s_tickn++;
}

#define C1_LINE_PAYLOAD 120u

typedef struct {
    char           id;
    const uint8_t *base;
    uint32_t       bytes;
} c1_sec_t;

static c1_sec_t s_secs[10];
static uint32_t s_sec_i, s_sec_off;
static uint32_t s_crc_all;

static uint32_t f32bits(float32_t f)
{
    uint32_t u;
    memcpy(&u, &f, 4u);
    return u;
}

static void c1_dump_begin(void)
{
    c1_sec_t secs[10] = {
        { 'X', (const uint8_t *)s_xp240,  sizeof s_xp240 },
        { 'D', (const uint8_t *)s_dstate, sizeof s_dstate },
        { 'G', (const uint8_t *)s_ring0,  sizeof s_ring0 },
        { 'R', (const uint8_t *)s_raw,    s_rawn * sizeof(int16_t) },
        { 'd', (const uint8_t *)s_dec,    s_decn * sizeof(int16_t) },
        { 'E', (const uint8_t *)s_res,    s_hopn * C1_HOP16 * sizeof(float32_t) },
        { 'C', (const uint8_t *)s_exc,    s_hopn * C1_HOP16 * sizeof(float32_t) },
        { 'M', (const uint8_t *)s_hopm,   s_hopn * sizeof(c1_hopmeta_t) },
        { 'F', (const uint8_t *)s_featbuf, s_tickn * SF0_NBINS_IN },
        { 'T', (const uint8_t *)s_tickc,  s_tickn * sizeof(uint32_t) },
    };
    memcpy(s_secs, secs, sizeof secs);
    s_sec_i = 0u;
    s_sec_off = 0u;
    s_crc_all = 0u;
    s_hdr_sent = 0u;
}

static void c1_emit_header(void)
{
    printf("C1|H|v=2;hops=%lu;raw=%lu;dec=%lu;ticks=%lu;nbins=%u;"
           "mode=%u;diag=%u;pre=%08lx;"
           "dovr=%lu;drbo=%lu;dfif=%lu\r\n",
           (unsigned long)s_hopn, (unsigned long)s_rawn,
           (unsigned long)s_decn, (unsigned long)s_tickn,
           (unsigned)SF0_NBINS_IN, (unsigned)s_mode0, (unsigned)s_diag0,
           (unsigned long)f32bits(s_pre),
           (unsigned long)(audio_overruns - s_ovr0[0]),
           (unsigned long)(rb_overruns   - s_ovr0[1]),
           (unsigned long)(fifo_underruns - s_ovr0[2]));
}

/* returns 0 after the final line */
static uint8_t c1_emit_next_line(void)
{
    static char hex[2u * C1_LINE_PAYLOAD + 1u];
    static const char nib[16] = "0123456789abcdef";

    while (s_sec_i < 10u && s_sec_off >= s_secs[s_sec_i].bytes) {
        s_sec_i++;
        s_sec_off = 0u;
    }
    if (s_sec_i >= 10u) {
        uint32_t total = 0u;
        for (uint32_t i = 0u; i < 10u; i++) total += s_secs[i].bytes;
        printf("C1|Z|%06lx|%08lx\r\n",
               (unsigned long)total, (unsigned long)s_crc_all);
        return 0u;
    }

    const c1_sec_t *s = &s_secs[s_sec_i];
    uint32_t n = s->bytes - s_sec_off;
    if (n > C1_LINE_PAYLOAD) n = C1_LINE_PAYLOAD;

    const uint8_t *p = s->base + s_sec_off;
    for (uint32_t i = 0u; i < n; i++) {
        hex[2u * i]      = nib[p[i] >> 4];
        hex[2u * i + 1u] = nib[p[i] & 0xFu];
    }
    hex[2u * n] = '\0';

    const uint32_t crc = crc32_update(0u, p, n);
    s_crc_all = crc32_update(s_crc_all, p, n);

    printf("C1|%c|%06lx|%s|%02lx\r\n",
           s->id, (unsigned long)s_sec_off, hex, (unsigned long)(crc & 0xFFu));
    s_sec_off += n;
    return 1u;
}

void C1_Service(void)
{
    switch (s_state) {
    case C1_IDLE:
    case C1_DONE:
        if (c1_arm) {
            c1_arm = 0u;
            s_state = C1_ARMED;
            printf("C1: armed (manual) -- capture starts at next hop\r\n");
        } else if (c1_auto && !s_auto_done) {
            if (Gate_State() == GATE_VOICED) {
                if (s_voiced_ms == 0u) {
                    s_voiced_ms = HAL_GetTick();
                } else if ((HAL_GetTick() - s_voiced_ms) >= 300u) {
                    s_auto_done = 1u;
                    s_state = C1_ARMED;
                    printf("C1: armed (auto, 300 ms voiced) -- keep singing"
                           " for %u ms + dump\r\n", (unsigned)(C1_HOPS * 5u));
                }
            } else {
                s_voiced_ms = 0u;
            }
        }
        break;

    case C1_DUMP: {
        if (!s_hdr_sent) {
            c1_dump_begin();
            if (Console_TxFree() < 1600u) return;
            c1_emit_header();
            s_hdr_sent = 1u;
            return;
        }
        /* leave room for a line plus the 1 Hz status lines (~700 chars),
         * or those get truncated */
        uint32_t lines = 0u;
        while (lines < 3u && Console_TxFree() >= 1600u) {
            if (!c1_emit_next_line()) {
                s_state = C1_DONE;
                printf("C1: dump complete -- %lu hops, %lu ticks, "
                       "overrun deltas a=%lu rb=%lu fifo=%lu. Save the log "
                       "and run tools/rsn_v3/c1/c1_parse.py\r\n",
                       (unsigned long)s_hopn, (unsigned long)s_tickn,
                       (unsigned long)(audio_overruns - s_ovr0[0]),
                       (unsigned long)(rb_overruns   - s_ovr0[1]),
                       (unsigned long)(fifo_underruns - s_ovr0[2]));
                break;
            }
            lines++;
        }
        break;
    }

    default:
        break;
    }
}

void C1_Init(void)
{
    s_state = C1_IDLE;
    s_auto_done = 0u;
    s_voiced_ms = 0u;
    printf("C1: cross-validation capture ready (auto=%u; or set c1_arm=1). "
           "Arena %lu KB in AXISRAM1\r\n", (unsigned)c1_auto,
           (unsigned long)((sizeof s_raw + sizeof s_dec + sizeof s_res +
                            sizeof s_exc + sizeof s_hopm + sizeof s_featbuf +
                            sizeof s_tickc + sizeof s_xp240 + sizeof s_dstate +
                            sizeof s_ring0) / 1024u));
}

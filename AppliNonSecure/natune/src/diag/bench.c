/*
 * bench.c - serial commands and the test bench (see bench.h)
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include "bench.h"
#include "console.h"
#include "voice_path.h"
#include "f0_logic.h"
#include "hf_path.h"
#include "rsn_npu.h"
#include "swiftf0_npu.h"
#include "natune_internal.h"
#include "main.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern volatile uint32_t uart_rx_ovr, uart_rx_drop;
extern volatile uint32_t uart_rx_last_ms;

static uint32_t stack_free_bytes(void)
{
  return natune_stack_free_min();
}

/* Bench buffers are in AXISRAM6, which neither network uses. */
#define AX6_BASE  0x24350000UL
#define CAP_MAX   72000u                            /* 1.5 s */
#define CLIP_MAX  72000u
static int16_t *const s_cin   = (int16_t *)(AX6_BASE);
static int16_t *const s_cout  = (int16_t *)(AX6_BASE + CAP_MAX * 2u);
static int16_t *const s_clip  = (int16_t *)(AX6_BASE + CAP_MAX * 4u);
_Static_assert(CAP_MAX * 4u + CLIP_MAX * 2u <= 0x70000u, "AXISRAM6 is 448 KB");
static uint32_t s_clip_n, s_clip_pos;
static float    s_clip_gain = 1.0f;

/* synthetic singer: polyBLEP saw through four formant resonators */
#define FS        48000.0f
#define N_FORM    4
#define SEQ_MAX   8

volatile uint8_t bench_src = 0;               /* 0 mic, 1 vowel, 2 sequence */

typedef struct { float a1, a2, b0, y1, y2; } reso_t;

static reso_t    s_res[N_FORM];
static float     s_phase, s_t, s_gain;
static float     s_f0 = 220.0f, s_vib_c = 0.0f, s_vib_hz = 5.5f;
static float     s_seq[SEQ_MAX];
static uint32_t  s_seq_n, s_seq_dwell, s_seq_i, s_seq_left;
static uint32_t  s_noise = 0x1234567u;
static uint32_t  s_am_period, s_am_pos;          /* on/off gating, 0 = steady */

/* /a/ formants and bandwidths */
static const float k_form[N_FORM][2] = {
  { 730.0f, 90.0f }, { 1090.0f, 110.0f }, { 2440.0f, 170.0f }, { 3400.0f, 250.0f },
};

static void reso_init(reso_t *r, float f, float bw)
{
  const float rr = expf(-3.14159265f * bw / FS);
  const float th = 6.28318531f * f / FS;
  r->a1 = 2.0f * rr * cosf(th);
  r->a2 = rr * rr;
  r->b0 = 1.0f - rr;
  r->y1 = r->y2 = 0.0f;
}

static inline float poly_blep(float t, float dt)
{
  if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
  return 0.0f;
}

static inline float gen_sample(void)
{
  float f = s_f0;
  if (s_vib_c > 0.0f)
  {
    f *= exp2f((s_vib_c / 1200.0f) * sinf(6.28318531f * s_vib_hz * s_t));
  }
  const float dt = f / FS;
  s_phase += dt;
  s_phase -= floorf(s_phase);
  float x = (2.0f * s_phase - 1.0f) - poly_blep(s_phase, dt);
  s_noise = s_noise * 1664525u + 1013904223u;            /* a little noise */
  x += 0.02f * ((float)(int32_t)s_noise * (1.0f / 2147483648.0f));
  for (int k = 0; k < N_FORM; k++)
  {
    reso_t *r = &s_res[k];
    const float y = r->b0 * x + r->a1 * r->y1 - r->a2 * r->y2;
    r->y2 = r->y1; r->y1 = y;
    x = y;
  }
  s_t += 1.0f / FS;
  if (s_t > 1000.0f) s_t -= 1000.0f;
  return x;
}

/* Configure and measure the output RMS so the requested level is what comes
 * out. The interrupt side is paused (bench_src = 0) meanwhile. */
static void synth_setup(float f0, float dbfs, float vib_c, float vib_hz)
{
  bench_src = 0u;
  __DMB();
  for (int k = 0; k < N_FORM; k++) reso_init(&s_res[k], k_form[k][0], k_form[k][1]);
  /* keep this short, it runs with the audio mutex held */
  s_f0 = f0; s_vib_c = 0.0f; s_vib_hz = vib_hz; s_phase = 0.0f; s_t = 0.0f;
  float acc = 0.0f;
  for (int i = 0; i < 1024; i++) { const float v = gen_sample(); acc += v * v; }
  const float rms = sqrtf(acc / 1024.0f);
  s_vib_c = vib_c;
  const float target = powf(10.0f, dbfs / 20.0f) * 0.35f;  /* roughly peak dBFS */
  s_gain = (rms > 1e-9f) ? target / rms : 0.0f;
}

static float s_noise_amp;

void Bench_FillBlock(int32_t *half, uint32_t n)
{
  if (!bench_src) return;
  if (bench_src == 4u)                       /* white noise */
  {
    for (uint32_t i = 0; i < n; i++)
    {
      s_noise = s_noise * 1664525u + 1013904223u;
      half[i] = (int32_t)((float)(int32_t)s_noise * (1.0f / 2147483648.0f) * s_noise_amp);
    }
    return;
  }
  if (bench_src == 3u)                       /* uploaded clip, looped */
  {
    for (uint32_t i = 0; i < n; i++)
    {
      float v = (float)s_clip[s_clip_pos] * s_clip_gain * 256.0f;
      if (v >  8388607.0f) v =  8388607.0f;
      if (v < -8388608.0f) v = -8388608.0f;
      half[i] = (int32_t)v;
      if (++s_clip_pos >= s_clip_n) s_clip_pos = 0u;
    }
    return;
  }
  if (bench_src == 2u && s_seq_n)
  {
    if (s_seq_left <= n)
    {
      s_seq_i = (s_seq_i + 1u) % s_seq_n;
      s_f0 = s_seq[s_seq_i];
      s_seq_left = s_seq_dwell;
    }
    else
    {
      s_seq_left -= n;
    }
  }
  const float g = s_gain * 8388608.0f;
  for (uint32_t i = 0; i < n; i++)
  {
    float v = gen_sample() * g;
    if (s_am_period)
    {
      if (s_am_pos >= s_am_period / 2u) v *= 0.0316f;      /* -30 dB */
      if (++s_am_pos >= s_am_period) s_am_pos = 0u;
    }
    if (v >  8388607.0f) v =  8388607.0f;
    if (v < -8388608.0f) v = -8388608.0f;
    half[i] = (int32_t)v;
  }
}

/* capture */
static uint32_t s_cap_n, s_cap_len;
static volatile uint8_t s_click;
#define HOPREC_MAX 320u                             /* 1.6 s of hops */
static int16_t  s_hop_corr[HOPREC_MAX];             /* cents re A4 */
static int16_t  s_hop_auth[HOPREC_MAX];
static uint32_t s_hop_n;
static uint8_t  s_cap_state;                        /* 0 idle 1 rec 2 dump */
static uint32_t s_dump_off, s_dump_ch, s_dump_lines;
static char     s_cap_desc[96];

/* per hop during a capture: target and tracked pitch, cents re A4, 0 = none */
void Bench_HopRecord(float f0_corr, float f0_auth)
{
  if (s_cap_state != 1u || s_hop_n >= HOPREC_MAX) return;
  s_hop_corr[s_hop_n] = (f0_corr > 0.0f) ? (int16_t)lrintf(1200.0f * log2f(f0_corr / 440.0f)) : 0;
  s_hop_auth[s_hop_n] = (f0_auth > 0.0f) ? (int16_t)lrintf(1200.0f * log2f(f0_auth / 440.0f)) : 0;
  s_hop_n++;
}

/* loopback latency test: a click 2 ms into the capture */
void Bench_Click(int16_t *spk, uint32_t n)
{
  if (!s_click || s_cap_state != 1u || s_cap_n < 96u) return;
  s_click = 0u;
  spk[0] = 28000; spk[1] = -28000;
  (void)n;
}

void Bench_Capture(const int32_t *mic, const int16_t *spk, uint32_t n)
{
  if (s_cap_state != 1u) return;
  for (uint32_t i = 0; i < n && s_cap_n < s_cap_len; i++, s_cap_n++)
  {
    s_cin[s_cap_n]  = (int16_t)__SSAT(mic[i] >> 8, 16);
    s_cout[s_cap_n] = spk[i];
  }
  if (s_cap_n >= s_cap_len)
  {
    s_cap_state = 2u;
    s_dump_off = 0u; s_dump_ch = 0u; s_dump_lines = 0u;
  }
}

static uint8_t crc8(const uint8_t *p, uint32_t n)
{
  uint8_t c = 0u;
  for (uint32_t i = 0; i < n; i++)
  {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (uint8_t)((c & 0x80u) ? (c << 1) ^ 0x07u : (c << 1));
  }
  return c;
}

static void emit_line(uint32_t ch, uint32_t off)
{
  static const char hx[] = "0123456789abcdef";
  const int16_t *src = ch ? s_cout : s_cin;
  uint32_t cnt = s_cap_len - off;
  if (cnt > 32u) cnt = 32u;
  const uint8_t *b = (const uint8_t *)&src[off];
  char line[200];
  int k = snprintf(line, sizeof line, "D1C|%c|%06lx|", ch ? 'O' : 'I', (unsigned long)off);
  for (uint32_t i = 0; i < cnt * 2u; i++)
  {
    line[k++] = hx[b[i] >> 4];
    line[k++] = hx[b[i] & 15u];
  }
  const uint8_t c = crc8(b, cnt * 2u);
  line[k++] = '|'; line[k++] = hx[c >> 4]; line[k++] = hx[c & 15u];
  line[k++] = '\r'; line[k++] = '\n'; line[k] = '\0';
  fputs(line, stdout);
  fflush(stdout);
}

static uint32_t s_refetch_ch, s_refetch_off;
static uint8_t  s_refetch;

static void dump_some(void)
{
  if (s_dump_off == 0u && s_dump_ch == 0u && s_dump_lines == 0u)
  {
    printf("D1C|H|n=%lu|fs=48000|%s|D=%u|dpath=%u\r\n", (unsigned long)s_cap_len,
           s_cap_desc, (unsigned)VOICE_PLAYOUT_BLOCKS, (unsigned)VOICE_D_PATH48);
    s_dump_lines = 1u;
  }
  while (s_cap_state == 2u && s_dump_ch < 2u && Console_TxFree() > 400u)
  {
    uint32_t cnt = s_cap_len - s_dump_off;
    if (cnt > 32u) cnt = 32u;
    emit_line(s_dump_ch, s_dump_off);
    s_dump_lines++;
    s_dump_off += cnt;
    if (s_dump_off >= s_cap_len)
    {
      s_dump_off = 0u;
      s_dump_ch++;                 /* input, output, then per-hop pitch */
    }
  }
  /* "D1C|F|<i>|<corr>,<auth>;..." 8 hops per line */
  while (s_cap_state == 2u && s_dump_ch == 2u && Console_TxFree() > 400u)
  {
    if (s_dump_off < s_hop_n)
    {
      char l[160];
      int k = snprintf(l, sizeof l, "D1C|F|%lu|", (unsigned long)s_dump_off);
      for (uint32_t j = s_dump_off; j < s_dump_off + 8u && j < s_hop_n; j++)
        k += snprintf(l + k, sizeof l - (size_t)k, "%d,%d;", s_hop_corr[j], s_hop_auth[j]);
      printf("%s\r\n", l);
      s_dump_off += 8u;
    }
    else
    {
      printf("D1C|Z|%lu\r\n", (unsigned long)s_dump_lines);
      s_cap_state = 0u;
    }
  }
}

/* "rec": one binary frame per 1 ms block over the console
 *   A5 5A | seq u16 LE | mode | payload | CRC16-CCITT of seq..payload
 *   mode 1: 48 x int16 output
 *   mode 2: 48 x (input, output) packed 12 + 12 bit
 * seq also counts dropped frames, so the host sees every gap. */
static volatile uint8_t s_rec;                      /* 0 off, 1 out, 2 in+out */
static uint8_t  s_rec_pending;
static uint16_t s_rec_seq;
static uint32_t s_rec_frames, s_rec_drops;
static uint16_t s_crc16_tab[256];

static uint16_t crc16(const uint8_t *p, uint32_t n)
{
  if (!s_crc16_tab[1])
  {
    for (uint32_t i = 0; i < 256u; i++)
    {
      uint16_t c = (uint16_t)(i << 8);
      for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
      s_crc16_tab[i] = c;
    }
  }
  uint16_t c = 0xFFFFu;
  for (uint32_t i = 0; i < n; i++) c = (uint16_t)((c << 8) ^ s_crc16_tab[((c >> 8) ^ p[i]) & 0xFFu]);
  return c;
}

void Bench_Stream(const int32_t *mic, const int16_t *spk, uint32_t n)
{
  const uint8_t mode = s_rec;
  if (!mode) return;
  uint8_t f[5u + 48u * 3u + 2u];
  uint32_t k = 0u;
  f[k++] = 0xA5u; f[k++] = 0x5Au;
  f[k++] = (uint8_t)(s_rec_seq & 0xFFu); f[k++] = (uint8_t)(s_rec_seq >> 8);
  f[k++] = mode;
  if (mode == 1u)
  {
    for (uint32_t i = 0; i < n; i++)
    {
      f[k++] = (uint8_t)((uint16_t)spk[i] & 0xFFu);
      f[k++] = (uint8_t)((uint16_t)spk[i] >> 8);
    }
  }
  else
  {
    for (uint32_t i = 0; i < n; i++)
    {
      const int32_t in12  = __SSAT(mic[i] >> 8, 16) >> 4;
      const int32_t out12 = (int32_t)spk[i] >> 4;
      f[k++] = (uint8_t)(in12 & 0xFF);
      f[k++] = (uint8_t)(((in12 >> 8) & 0x0F) | ((out12 & 0x0F) << 4));
      f[k++] = (uint8_t)((out12 >> 4) & 0xFF);
    }
  }
  const uint16_t c = crc16(&f[2], k - 2u);
  f[k++] = (uint8_t)(c & 0xFFu); f[k++] = (uint8_t)(c >> 8);
  if (Console_WriteRaw(f, k)) s_rec_frames++; else s_rec_drops++;
  s_rec_seq++;
}

static char s_reply[240];
static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* CRC32 of the uploaded clip, 8 KB per call so the audio task is not held up */
static uint32_t s_crc_tab[256];
static uint32_t s_crc, s_crc_pos;
static uint8_t  s_crc_busy;

static void crc_step(void)
{
  if (!s_crc_tab[1])
  {
    for (uint32_t i = 0; i < 256u; i++)
    {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      s_crc_tab[i] = c;
    }
  }
  const uint8_t *b = (const uint8_t *)s_clip;
  const uint32_t end = s_clip_n * 2u;
  uint32_t lim = s_crc_pos + 8192u;
  if (lim > end) lim = end;
  uint32_t c = s_crc;
  for (uint32_t i = s_crc_pos; i < lim; i++) c = s_crc_tab[(c ^ b[i]) & 0xFFu] ^ (c >> 8);
  s_crc = c; s_crc_pos = lim;
  if (lim >= end)
  {
    s_crc_busy = 0u;
    reply("OK clipcrc %08lx n %lu", (unsigned long)(s_crc ^ 0xFFFFFFFFu), (unsigned long)s_clip_n);
  }
}

static void reply(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_reply, sizeof s_reply, fmt, ap);
  va_end(ap);
}

static const char *mode_name(void)
{
  if (!voice_mode) return "bypass";
  return voice_rsn ? "rsn" : "plain";
}

static void cmd(char *l)
{
  char *argv[12];
  int argc = 0;
  for (char *t = strtok(l, " \t"); t && argc < 12; t = strtok(NULL, " \t")) argv[argc++] = t;
  if (argc == 0) return;

  if (!strcmp(argv[0], "src") && argc >= 2)
  {
    if (!strcmp(argv[1], "mic")) { bench_src = 0u; reply("OK src mic"); return; }
    if (!strcmp(argv[1], "vowel") && argc >= 3)
    {
      const float f0 = strtof(argv[2], NULL);
      const float db = (argc >= 4) ? strtof(argv[3], NULL) : -12.0f;
      const float vc = (argc >= 5) ? strtof(argv[4], NULL) : 0.0f;
      const float vh = (argc >= 6) ? strtof(argv[5], NULL) : 5.5f;
      const int   am = (argc >= 7) ? atoi(argv[6]) : 0;       /* on/off period, ms */
      if (f0 < 60.0f || f0 > 1000.0f) { reply("ERR f0 60..1000"); return; }
      synth_setup(f0, db, vc, vh);
      s_seq_n = 0u;
      s_am_period = (am > 0) ? (uint32_t)am * 48u : 0u;
      s_am_pos = 0u;
      bench_src = 1u;
      snprintf(s_cap_desc, sizeof s_cap_desc, "src=vowel|f0c=%ld|dbc=%ld|vibc=%ld|vibhzc=%ld",
               (long)lrintf(f0 * 100.0f), (long)lrintf(db * 100.0f),
               (long)lrintf(vc * 100.0f), (long)lrintf(vh * 100.0f));
      reply("OK src vowel %s", s_cap_desc);
      return;
    }
    if (!strcmp(argv[1], "noise"))
    {
      const float db = (argc >= 3) ? strtof(argv[2], NULL) : -20.0f;
      s_noise_amp = powf(10.0f, db / 20.0f) * 1.7320508f * 8388608.0f;
      snprintf(s_cap_desc, sizeof s_cap_desc, "src=noise|dbc=%ld", (long)lrintf(db * 100.0f));
      bench_src = 4u;
      reply("OK src noise %s", s_cap_desc);
      return;
    }
    if (!strcmp(argv[1], "seq") && argc >= 4)
    {
      const uint32_t dwell = (uint32_t)atoi(argv[2]);
      s_seq_n = 0u;
      for (int i = 3; i < argc && s_seq_n < SEQ_MAX; i++) s_seq[s_seq_n++] = strtof(argv[i], NULL);
      synth_setup(s_seq[0], -12.0f, 0.0f, 5.5f);
      s_am_period = 0u;
      s_seq_i = 0u; s_seq_dwell = dwell * 48u; s_seq_left = s_seq_dwell;
      bench_src = 2u;
      int k = snprintf(s_cap_desc, sizeof s_cap_desc, "src=seq|dwell=%lu|f0c=", (unsigned long)dwell);
      for (uint32_t i = 0; i < s_seq_n && k < (int)sizeof s_cap_desc - 8; i++)
        k += snprintf(s_cap_desc + k, sizeof s_cap_desc - (size_t)k, "%s%ld", i ? "," : "",
                      (long)lrintf(s_seq[i] * 100.0f));
      reply("OK src seq dwell %lu ms, %lu notes", (unsigned long)dwell, (unsigned long)s_seq_n);
      return;
    }
  }
  if (!strcmp(argv[0], "cap") && argc >= 2)
  {
    uint32_t ms = (uint32_t)atoi(argv[1]);
    uint32_t n = ms * 48u;
    if (n > CAP_MAX) n = CAP_MAX;
    n -= n % 48u;
    if (s_cap_state) { reply("ERR capture busy"); return; }
    if (s_rec) { reply("ERR recording (rec 0 first)"); return; }
    if (!bench_src) snprintf(s_cap_desc, sizeof s_cap_desc, "src=mic");
    char *e = s_cap_desc + strlen(s_cap_desc);
    snprintf(e, sizeof s_cap_desc - (size_t)(e - s_cap_desc), "|mode=%s|key=%u|scale=%03x",
             mode_name(), (unsigned)f0_key_root, (unsigned)f0_scale_mask);
    if (bench_src == 3u)
    {
      e = s_cap_desc + strlen(s_cap_desc);
      snprintf(e, sizeof s_cap_desc - (size_t)(e - s_cap_desc), "|clippos=%lu", (unsigned long)s_clip_pos);
    }
    s_cap_len = n; s_cap_n = 0u; s_hop_n = 0u;
    __DMB();
    s_cap_state = 1u;
    reply("OK cap %lu samples", (unsigned long)n);
    return;
  }
  if (!strcmp(argv[0], "clipn") && argc >= 2)      /* start an upload of n samples */
  {
    const uint32_t nn = (uint32_t)strtoul(argv[1], NULL, 10);
    if (nn == 0u || nn > CLIP_MAX) { reply("ERR clipn 1..%lu", (unsigned long)CLIP_MAX); return; }
    if (bench_src == 3u) bench_src = 0u;
    s_clip_n = nn; s_clip_pos = 0u;
    reply("OK clipn %lu", (unsigned long)nn);
    return;
  }
  if (!strcmp(argv[0], "L") && argc >= 3)          /* L <offset> <hex bytes> */
  {
    const uint32_t off = (uint32_t)strtoul(argv[1], NULL, 16);
    const char *h = argv[2];
    uint8_t *dst = (uint8_t *)&s_clip[off];
    uint32_t nb = 0u;
    while (h[0] && h[1] && off * 2u + nb < s_clip_n * 2u)
    {
      const char hi = h[0], lo = h[1];
      const uint8_t v = (uint8_t)(((hi <= '9' ? hi - '0' : (hi | 32) - 'a' + 10) << 4) |
                                   (lo <= '9' ? lo - '0' : (lo | 32) - 'a' + 10));
      dst[nb++] = v;
      h += 2;
    }
    return;                                        /* no reply, the host checks the CRC */
  }
  if (!strcmp(argv[0], "clipcrc"))
  {
    s_crc = 0xFFFFFFFFu; s_crc_pos = 0u; s_crc_busy = 1u;
    return;
  }
  if (!strcmp(argv[0], "clip") && argc >= 2 && !strcmp(argv[1], "play"))
  {
    if (!s_clip_n) { reply("ERR no clip"); return; }
    s_clip_gain = (argc >= 3) ? powf(10.0f, strtof(argv[2], NULL) / 20.0f) : 1.0f;
    s_clip_pos = 0u;
    snprintf(s_cap_desc, sizeof s_cap_desc, "src=clip|n=%lu", (unsigned long)s_clip_n);
    bench_src = 3u;
    reply("OK clip play %lu", (unsigned long)s_clip_n);
    return;
  }
  if (!strcmp(argv[0], "click"))                   /* 60 ms capture with a click */
  {
    if (s_cap_state) { reply("ERR capture busy"); return; }
    snprintf(s_cap_desc, sizeof s_cap_desc, "src=click|mode=%s", mode_name());
    s_cap_len = 60u * 48u; s_cap_n = 0u; s_hop_n = 0u;
    s_click = 1u;
    __DMB();
    s_cap_state = 1u;
    reply("OK click");
    return;
  }
  if (!strcmp(argv[0], "rec") && argc >= 2)        /* rec 1 | rec 2 | rec 0 */
  {
    const int m = atoi(argv[1]);
    if (m == 0)
    {
      s_rec = 0u; s_rec_pending = 0u;
      console_mute = 0u;
      reply("OK rec 0 frames %lu dropped %lu", (unsigned long)s_rec_frames, (unsigned long)s_rec_drops);
      return;
    }
    if (m != 1 && m != 2) { reply("ERR rec 1 (out) | 2 (in+out) | 0 (stop)"); return; }
    if (s_cap_state) { reply("ERR capture busy"); return; }
    s_rec_seq = 0u; s_rec_frames = 0u; s_rec_drops = 0u;
    s_rec_pending = (uint8_t)m;                    /* starts after the reply */
    reply("OK rec %d", m);
    return;
  }
  if (!strcmp(argv[0], "line") && argc >= 3)       /* re-send one dump line */
  {
    s_refetch_ch = (argv[1][0] == 'O') ? 1u : 0u;
    s_refetch_off = (uint32_t)strtoul(argv[2], NULL, 16);
    if (s_refetch_off >= s_cap_len || (s_refetch_off & 31u)) { reply("ERR line offset"); return; }
    s_refetch = 1u;
    return;
  }
  if (!strcmp(argv[0], "mode") && argc >= 2)
  {
    if (!strcmp(argv[1], "rsn"))         { voice_rsn = 1u; voice_mode = 1u; }
    else if (!strcmp(argv[1], "plain"))  { voice_rsn = 0u; voice_mode = 1u; }
    else if (!strcmp(argv[1], "bypass")) { voice_mode = 0u; }
    else { reply("ERR mode rsn|plain|bypass"); return; }
    reply("OK mode %s", mode_name());
    return;
  }
  if (!strcmp(argv[0], "key") && argc >= 2)
  {
    const int k = atoi(argv[1]);
    if (k < 0 || k > 11) { reply("ERR key 0..11"); return; }
    f0_key_root = (uint8_t)k;
    reply("OK key %d", k);
    return;
  }
  if (!strcmp(argv[0], "scale") && argc >= 2)
  {
    uint16_t m;
    if (!strcmp(argv[1], "chrom"))      m = 0x0FFFu;
    else if (!strcmp(argv[1], "major")) m = 0x0AB5u;
    else if (!strcmp(argv[1], "minor")) m = 0x05ADu;
    else m = (uint16_t)(strtoul(argv[1], NULL, 16) & 0x0FFFu);
    if (!m) { reply("ERR empty scale"); return; }
    f0_scale_mask = m;
    reply("OK scale %03x", (unsigned)m);
    return;
  }
  if (!strcmp(argv[0], "vib") && argc >= 2)
  {
    if (!strcmp(argv[1], "ratio"))     { f0_vibrato_ratio = 1u; }
    else if (!strcmp(argv[1], "snap")) { f0_vibrato_ratio = 0u; f0_vibrato_snap_mean = 1u; }
    else if (!strcmp(argv[1], "raw"))  { f0_vibrato_ratio = 0u; f0_vibrato_snap_mean = 0u; }
    else { reply("ERR vib raw|snap|ratio"); return; }
    reply("OK vib %s", argv[1]);
    return;
  }
  if (argc >= 2 && (!strcmp(argv[0], "hf") || !strcmp(argv[0], "agc") || !strcmp(argv[0], "route") ||
                    !strcmp(argv[0], "vsnap") || !strcmp(argv[0], "trim") || !strcmp(argv[0], "hfcomp")))
  {
    const int v = atoi(argv[1]);
    if (!strcmp(argv[0], "hf"))    hf_enabled = (uint8_t)(v != 0);
    if (!strcmp(argv[0], "agc"))   voice_agc = (uint8_t)(v != 0);
    if (!strcmp(argv[0], "route")) gate_routing = (uint8_t)(v != 0);
    if (!strcmp(argv[0], "vsnap")) f0_vibrato_snap_mean = (uint8_t)(v != 0);
    if (!strcmp(argv[0], "trim"))  hf_d3_trim = (int8_t)(v < -16 ? -16 : v > 16 ? 16 : v);
    if (!strcmp(argv[0], "hfcomp")) hf_complementary = (uint8_t)(v != 0);
    reply("OK %s %d", argv[0], v);
    return;
  }
  if (!strcmp(argv[0], "agct") && argc >= 2)       /* AGC target x100 */
  {
    const int v = atoi(argv[1]);
    if (v < 3 || v > 80) { reply("ERR agct 3..80 (x0.01)"); return; }
    voice_agc_target = (float)v * 0.01f;
    reply("OK agct %d", v);
    return;
  }
  if (!strcmp(argv[0], "makeup") && argc >= 2)     /* output trim, dB x10 */
  {
    const int v = atoi(argv[1]);
    if (v < -200 || v > 200) { reply("ERR makeup -200..200 (x0.1 dB)"); return; }
    voice_makeup_db = (float)v * 0.1f;
    reply("OK makeup %d", v);
    return;
  }
  if (!strcmp(argv[0], "stat"))
  {
    reply("OK stat mode=%s src=%u key=%u scale=%03x hf=%u agc=%u route=%u vib=%s trim=%d "
          "rsn_ready=%u misses=%lu overruns=%lu rx_ovr=%lu rx_drop=%lu stack_free=%lu",
          mode_name(), (unsigned)bench_src, (unsigned)f0_key_root, (unsigned)f0_scale_mask,
          (unsigned)hf_enabled, (unsigned)voice_agc, (unsigned)gate_routing,
          f0_vibrato_ratio ? "ratio" : (f0_vibrato_snap_mean ? "snap" : "raw"), (int)hf_d3_trim, (unsigned)rsn_npu_ready(),
          (unsigned long)fifo_underruns, (unsigned long)audio_overruns,
          (unsigned long)uart_rx_ovr, (unsigned long)uart_rx_drop,
          (unsigned long)stack_free_bytes());
    return;
  }
  reply("ERR unknown '%s' (src/clip/cap/mode/key/scale/vib/hf/agc/agct/makeup/route/trim/stat)", argv[0]);
}

void Bench_Service(void)
{
  if (s_crc_busy) crc_step();
  /* the recorder sends a keepalive every second; stop if it goes away */
  if (s_rec && (HAL_GetTick() - uart_rx_last_ms) > 3000u)
  {
    s_rec = 0u;
    console_mute = 0u;
    reply("rec: stopped (no keepalive from the host for 3 s); frames %lu dropped %lu",
          (unsigned long)s_rec_frames, (unsigned long)s_rec_drops);
  }
  static char line[200];
  if (Console_GetLine(line, (int)sizeof line) > 0)
  {
    cmd(line);
  }
}

void Bench_PrintSlot(void)
{
  if (s_reply[0])
  {
    if (console_mute)
    {
      /* while streaming, replies go out between frames */
      char l[260];
      const int k = snprintf(l, sizeof l, "\r\n%s\r\n", s_reply);
      if (Console_WriteRaw((const uint8_t *)l, (uint32_t)k)) s_reply[0] = '\0';
    }
    else
    {
      printf("%s\r\n", s_reply);
      s_reply[0] = '\0';
    }
  }
  if (s_rec_pending)
  {
    console_mute = 1u;
    s_rec = s_rec_pending;
    s_rec_pending = 0u;
  }
  if (s_cap_state == 2u)
  {
    dump_some();
  }
  if (s_refetch && s_cap_state == 0u && s_cap_len)
  {
    emit_line(s_refetch_ch, s_refetch_off);
    s_refetch = 0u;
  }
}

void Bench_Init(void)
{
  for (int k = 0; k < N_FORM; k++) reso_init(&s_res[k], k_form[k][0], k_form[k][1]);
  printf("Bench: UART commands ready (src/clip/cap/mode/key/scale/vib/hf/agc/agct/makeup/route/trim/stat)\r\n");
}

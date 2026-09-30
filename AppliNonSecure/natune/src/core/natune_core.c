/*
 * natune_core.c - tasks, kernel objects and the public API
 *
 * npu task    NPU service (npu_service.c), highest priority
 * audio task  one pass per 1 ms capture block
 * ctrl task   serial shell, bench, status lines
 *
 * audio and ctrl share the DSP state under a mutex. The npu task only talks
 * to them through single-producer/single-consumer queues, so it can preempt
 * both.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include <string.h>
#include <stdio.h>
#include <arm_mve.h>

#include "natune.h"
#include "natune_internal.h"
#include "natune_port.h"
#include "main.h"

#include "spectrum.h"
#include "hf_path.h"
#include "voice_path.h"
#include "gate.h"
#include "f0_logic.h"
#include "excitation.h"
#include "swiftf0_npu.h"
#include "rsn_npu.h"
#include "npu_service.h"
#include "bench.h"
#include "c1_capture.h"
#include "console.h"

#define HALF_BUFFER	NATUNE_BLOCK

_Static_assert(HALF_BUFFER == SPECTRUM_BLOCK_SAMPLES, "spectrum.c expects 48-sample blocks");
_Static_assert(HALF_BUFFER == HF_BLOCK_SAMPLES, "hf_path.c expects 48-sample blocks");
_Static_assert(HALF_BUFFER == VOICE_BLOCK48, "voice_path.c expects 48-sample blocks");

#define EV_BLOCK	0x0001U
#define STK_PAINT	0xC0DEF00DU
#define CLIP_LEVEL	29205		/* -1 dBFS */

static volatile uint8_t  process_ping, process_pong;
static volatile uint32_t audio_blk_seq;
static volatile uint32_t half_seq[2];

/* blocks dropped because the audio task was still busy with the last one */
volatile uint32_t audio_overruns = 0;

/* 1: unvoiced blocks play the dry input, 0: always the voice path */
volatile uint8_t gate_routing = 1u;

/* worst-case timings since the last BUDGET line, in cycles */
volatile uint32_t loop_max_pass = 0, loop_max_blk = 0, loop_max_pump = 0, loop_max_sf0 = 0;

static ID s_flg = 0, s_mtx = 0, s_tsk_audio = 0, s_tsk_ctrl = 0;
static UINT s_flags;
static BOOL s_running;
static volatile uint32_t s_blocks, s_clips;

static uint32_t s_stk_npu[NATUNE_NPU_STKSZ / 4];
static uint32_t s_stk_audio[NATUNE_AUDIO_STKSZ / 4];
static uint32_t s_stk_ctrl[NATUNE_CTRL_STKSZ / 4];

/* capture DMA interrupt, once per 48-sample half buffer */
void natune_isr_block(int32_t *half, UINT h)
{
	volatile uint8_t *flag = h ? &process_pong : &process_ping;

	if (*flag) {
		audio_overruns++;
	}
	const uint32_t q = audio_blk_seq++;
	half_seq[h] = q;
	*flag = 1u;
	Bench_FillBlock(half, HALF_BUFFER);	/* test signal, if selected */
	Spectrum_PushBlock(half, q);
	if (s_flg > 0) {
		tk_set_flg(s_flg, EV_BLOCK);
	}
}

static void note_block(const int32_t *mic, const int16_t *spk, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		int32_t a = mic[i] >> 8, b = spk[i];
		if (a < 0) a = -a;
		if (b < 0) b = -b;
		if (a >= CLIP_LEVEL || b >= CLIP_LEVEL) {
			s_clips++;
			return;
		}
	}
}

/* mic: 24-bit right-aligned samples from the MDF, spk: what the SAI plays */
static void Audio_ProcessBlock(const int32_t *mic, int16_t *spk, uint32_t n, uint32_t seq)
{
	if (natune_port_tx_collision(spk)) {
		audio_overruns++;
	}

	/* the gate runs in every mode so its history stays warm */
	const gate_state_t gstate = Gate_PushBlock(mic, n);

	if (voice_mode) {
		/* Unvoiced blocks, pre-roll and long concealment play the raw
		 * input delayed to line up with the voice path; everything else
		 * plays the voice path. Switching crossfades over one block. */
		int16_t vbuf[HALF_BUFFER];
		int16_t rbuf[HALF_BUFFER];
		const uint8_t vst = Voice_ConsumeBlock(vbuf, n, seq);
		HF_Write(mic, n);
		HF_RawAligned(rbuf, n);

		const uint8_t use_raw = (uint8_t)((gate_routing && gstate == GATE_UNVOICED) ||
						  vst == VOICE_PREROLL || vst == VOICE_TIER2);
		static uint8_t prev_raw = 1u;
		if (use_raw != prev_raw) {
			const int16_t *from = use_raw ? vbuf : rbuf;
			const int16_t *to   = use_raw ? rbuf : vbuf;
			for (uint32_t x = 0; x < n; x++) {
				const float w = (float)x / (float)n;
				spk[x] = (int16_t)__SSAT((int32_t)((1.0f - w) * (float)from[x] +
								   w * (float)to[x]), 16);
			}
			prev_raw = use_raw;
		} else {
			memcpy(spk, use_raw ? rbuf : vbuf, n * sizeof(int16_t));
		}
		(void)Gate_ConsumeTransition();

		HF_Mix(spk, n, (uint8_t)!use_raw);
		Bench_Click(spk, n);
		Bench_Capture(mic, spk, n);
		Bench_Stream(mic, spk, n);
		note_block(mic, spk, n);
		return;
	}

	/* bypass: keep the voice path's play-out clock running */
	{
		int16_t discard[HALF_BUFFER];
		(void)Voice_ConsumeBlock(discard, n, seq);
	}

	/* 24 -> 16 bit with MVE. VQSHRNB/T write the even/odd lanes, so load
	 * with VLD2 to split even/odd samples first. */
	uint32_t i = 0;
	for (; i + 8 <= n; i += 8) {
		int32x4x2_t micBlock = vld2q_s32(&mic[i]);
		int16x8_t pcmBlock = vqshrnbq(vuninitializedq_s16(), micBlock.val[0], 8);
		pcmBlock = vqshrntq(pcmBlock, micBlock.val[1], 8);
		vst1q(&spk[i], pcmBlock);
	}
	for (; i < n; i++) {
		spk[i] = (int16_t)__SSAT(mic[i] >> 8, 16);
	}

	HF_ProcessBlockGated(mic, spk, n, 0u);	/* keep the delay line warm, no mix */
	(void)Gate_ConsumeTransition();
	Bench_Click(spk, n);
	Bench_Capture(mic, spk, n);
	Bench_Stream(mic, spk, n);
	note_block(mic, spk, n);
}

static void audio_pass(void)
{
	UINT imask;

	/* Take the flags before pumping the ring. A block that arrives during
	 * this pass is then handled entirely in the next one. */
	DI(imask);
	const uint8_t do_ping = process_ping;  process_ping = 0u;
	const uint8_t do_pong = process_pong;  process_pong = 0u;
	EI(imask);

	/* Start the speaker once the first hop exists, on a pong pass: the mic
	 * DMA has just wrapped to index 0, which is where the SAI starts. */
	if (!s_running && do_pong && Voice_FirstWriteDone()) {
		s_running = TRUE;
		natune_port_start_playback();
	}

	const uint32_t t_pass = DWT->CYCCNT;
	Spectrum_Pump();
	const uint32_t d_pump = DWT->CYCCNT - t_pass;
	if (d_pump > loop_max_pump) { loop_max_pump = d_pump; }

	for (UINT h = 0; h < 2U; h++) {
		if (h ? do_pong : do_ping) {
			const uint32_t t_b = DWT->CYCCNT;
			Audio_ProcessBlock(natune_port_mic_half(h), natune_port_spk_half(h),
					   HALF_BUFFER, half_seq[h]);
			const uint32_t d_b = DWT->CYCCNT - t_b;
			if (d_b > loop_max_blk) { loop_max_blk = d_b; }
			s_blocks++;
		}
	}

	/* pitch tracker front-end; the NPU part runs in the npu task */
	const uint32_t t_sf0 = DWT->CYCCNT;
	sf0_npu_service();
	const uint32_t d_sf0 = DWT->CYCCNT - t_sf0;
	if (d_sf0 > loop_max_sf0) { loop_max_sf0 = d_sf0; }

	const uint32_t d_pass = DWT->CYCCNT - t_pass;
	if (d_pass > loop_max_pass) { loop_max_pass = d_pass; }
}

static void audio_task(INT stacd, void *exinf)
{
	UINT ptn;

	(void)stacd;
	(void)exinf;
	for (;;) {
		tk_wai_flg(s_flg, EV_BLOCK, TWF_ORW | TWF_BITCLR, &ptn, TMO_FEVR);
		tk_loc_mtx(s_mtx, TMO_FEVR);
		audio_pass();
		tk_unl_mtx(s_mtx);
		if (s_tsk_ctrl > 0) {
			tk_wup_tsk(s_tsk_ctrl);
		}
	}
}

static void status_lines(void)
{
	static uint32_t next_sf0 = 2000u;

	Voice_PrintTick();
	if ((int32_t)(HAL_GetTick() - next_sf0) >= 0) {
		next_sf0 = HAL_GetTick() + 1000u;
		char line[160];
		sf0_npu_format(line, sizeof line);
		printf("%s\r\n", line);
		printf("BUDGET worst/pass %lu cyc (%lu us of 1000) | blk %lu | pump+hop %lu | "
		       "sf0 %lu | overruns %lu | stack free %lu B\r\n",
		       (unsigned long)loop_max_pass, (unsigned long)(loop_max_pass / 600u),
		       (unsigned long)loop_max_blk,  (unsigned long)loop_max_pump,
		       (unsigned long)loop_max_sf0,  (unsigned long)audio_overruns,
		       (unsigned long)natune_stack_free_min());
		loop_max_pass = loop_max_blk = loop_max_pump = loop_max_sf0 = 0;
	}
}

static void ctrl_task(INT stacd, void *exinf)
{
	(void)stacd;
	(void)exinf;
	for (;;) {
		tk_slp_tsk(1);			/* the audio task wakes us */
		tk_can_wup(TSK_SELF);
		tk_loc_mtx(s_mtx, TMO_FEVR);

		C1_Service();

		/* print only right after a synthesis hop, when the next deadline
		 * is furthest away */
		if (Voice_TakePrintSlot()) {
			if (!(s_flags & NATUNE_CFG_QUIET)) {
				status_lines();
			}
			if (!(s_flags & NATUNE_CFG_NOSHELL)) {
				Bench_PrintSlot();
			}
		}
		if (!(s_flags & NATUNE_CFG_NOSHELL)) {
			Bench_Service();
		}
		tk_unl_mtx(s_mtx);
	}
}

/* stacks are painted so the high-water mark can be read back */
static void paint(uint32_t *stk, uint32_t words)
{
	for (uint32_t i = 0; i < words; i++) {
		stk[i] = STK_PAINT;
	}
}

static uint32_t headroom(const uint32_t *stk, uint32_t words)
{
	uint32_t n = 0;
	while (n < words && stk[n] == STK_PAINT) {
		n++;
	}
	return n * 4u;
}

uint32_t natune_stack_free_min(void)
{
	uint32_t m = headroom(s_stk_npu, NATUNE_NPU_STKSZ / 4);
	uint32_t a = headroom(s_stk_audio, NATUNE_AUDIO_STKSZ / 4);
	uint32_t c = headroom(s_stk_ctrl, NATUNE_CTRL_STKSZ / 4);
	if (a < m) m = a;
	if (c < m) m = c;
	return m;
}

static ID make_task(FP entry, PRI pri, uint32_t *stk, SZ stksz)
{
	T_CTSK ctsk;

	paint(stk, (uint32_t)stksz / 4u);
	ctsk.exinf = NULL;
	ctsk.tskatr = TA_HLNG | TA_RNG0 | TA_USERBUF | TA_FPU;
	ctsk.task = entry;
	ctsk.itskpri = pri;
	ctsk.stksz = stksz;
	ctsk.bufptr = stk;
	return tk_cre_tsk(&ctsk);
}

#define IN_TASK()	(__get_IPSR() == 0U)

ER natune_init(const T_NATUNE_CFG *pk_cfg)
{
	static const T_NATUNE_CFG defcfg;
	const T_NATUNE_CFG *cfg = (pk_cfg != NULL) ? pk_cfg : &defcfg;
	const PRI npu_pri   = cfg->npu_pri   ? cfg->npu_pri   : NATUNE_NPU_PRI;
	const PRI audio_pri = cfg->audio_pri ? cfg->audio_pri : NATUNE_AUDIO_PRI;
	const PRI ctrl_pri  = cfg->ctrl_pri  ? cfg->ctrl_pri  : NATUNE_CTRL_PRI;
	ER er;

	if (!IN_TASK()) {
		return E_CTX;
	}
	if (s_flg > 0) {
		return E_OBJ;
	}
	if (!(npu_pri < audio_pri && audio_pri < ctrl_pri)) {
		return E_PAR;
	}
	if (cfg->key_root < 0 || cfg->key_root > 11 || cfg->scale_mask > 0x0FFFU) {
		return E_PAR;
	}
	s_flags = cfg->flags;

	er = natune_port_kernel_init();
	if (er < E_OK) {
		return er;
	}
	er = NPU_ServiceDefineInt();		/* before the runtime enables the IRQ */
	if (er < E_OK) {
		return er;
	}

	if (natune_port_codec_init() < E_OK) {
		printf("natune: codec init reported errors (continuing)\r\n");
	}
	Spectrum_Init();
	HF_Init();
	Gate_Init();
	F0_Init();
	Exc_Init();
	Voice_Init();
	Bench_Init();

	/* Both run blocking, before any task exists. If the RSN self-test
	 * fails the voice path falls back to the plain LPC round trip. */
	sf0_npu_init();
	rsn_npu_init();
	C1_Init();

	if (cfg->flags & NATUNE_CFG_MODE) {
		natune_set_mode(cfg->mode);
	}
	f0_key_root = (uint8_t)cfg->key_root;
	f0_scale_mask = (uint16_t)(cfg->scale_mask ? cfg->scale_mask : NATUNE_SCALE_CHROMATIC);

	{
		T_CMTX cmtx;
		cmtx.exinf = NULL;
		cmtx.mtxatr = TA_INHERIT;
		cmtx.ceilpri = 0;
		s_mtx = tk_cre_mtx(&cmtx);
		if (s_mtx < E_OK) {
			return s_mtx;
		}
	}
	paint(s_stk_npu, NATUNE_NPU_STKSZ / 4);	/* must happen before tk_sta_tsk */
	er = NPU_ServiceStart(npu_pri, s_stk_npu, NATUNE_NPU_STKSZ);
	if (er < E_OK) {
		return er;
	}

	s_tsk_audio = make_task((FP)audio_task, audio_pri, s_stk_audio, NATUNE_AUDIO_STKSZ);
	if (s_tsk_audio < E_OK) {
		return s_tsk_audio;
	}
	s_tsk_ctrl = make_task((FP)ctrl_task, ctrl_pri, s_stk_ctrl, NATUNE_CTRL_STKSZ);
	if (s_tsk_ctrl < E_OK) {
		return s_tsk_ctrl;
	}
	{
		T_CFLG cflg;
		cflg.exinf = NULL;
		cflg.flgatr = TA_TFIFO | TA_WSGL;
		cflg.iflgptn = 0;
		er = tk_cre_flg(&cflg);
		if (er < E_OK) {
			return er;
		}
		s_flg = er;
	}
	er = tk_sta_tsk(s_tsk_audio, 0);
	if (er >= E_OK) {
		er = tk_sta_tsk(s_tsk_ctrl, 0);
	}
	return (er < E_OK) ? er : E_OK;
}

ER natune_start(void)
{
	if (!IN_TASK()) {
		return E_CTX;
	}
	if (s_flg <= 0) {
		return E_OBJ;
	}
	return natune_port_start_capture();	/* the audio task starts the speaker */
}

ER natune_set_mode(INT mode)
{
	if (!IN_TASK()) {
		return E_CTX;
	}
	if (mode < NATUNE_MODE_BYPASS || mode > NATUNE_MODE_RSN) {
		return E_PAR;
	}
	if (s_mtx > 0) tk_loc_mtx(s_mtx, TMO_FEVR);
	if (mode == NATUNE_MODE_BYPASS) {
		voice_mode = 0u;
	} else {
		voice_rsn = (mode == NATUNE_MODE_RSN) ? 1u : 0u;
		voice_mode = 1u;
	}
	if (s_mtx > 0) tk_unl_mtx(s_mtx);
	return E_OK;
}

ER natune_set_key(INT key_root, UINT scale_mask)
{
	if (!IN_TASK()) {
		return E_CTX;
	}
	if (key_root < 0 || key_root > 11 || scale_mask == 0U || scale_mask > 0x0FFFU) {
		return E_PAR;
	}
	if (s_mtx > 0) tk_loc_mtx(s_mtx, TMO_FEVR);
	f0_key_root = (uint8_t)key_root;
	f0_scale_mask = (uint16_t)scale_mask;
	if (s_mtx > 0) tk_unl_mtx(s_mtx);
	return E_OK;
}

ER natune_set_vibrato(INT vib)
{
	if (!IN_TASK()) {
		return E_CTX;
	}
	if (s_mtx > 0) tk_loc_mtx(s_mtx, TMO_FEVR);
	ER er = E_OK;
	switch (vib) {
	case NATUNE_VIB_RATIO: f0_vibrato_ratio = 1u; break;
	case NATUNE_VIB_SNAP:  f0_vibrato_ratio = 0u; f0_vibrato_snap_mean = 1u; break;
	case NATUNE_VIB_RAW:   f0_vibrato_ratio = 0u; f0_vibrato_snap_mean = 0u; break;
	default:               er = E_PAR; break;
	}
	if (s_mtx > 0) tk_unl_mtx(s_mtx);
	return er;
}

ER natune_ref_sts(T_NATUNE_STS *pk_sts)
{
	f0_state_t f0;

	if (!IN_TASK()) {
		return E_CTX;
	}
	if (pk_sts == NULL) {
		return E_PAR;
	}
	if (s_mtx > 0) tk_loc_mtx(s_mtx, TMO_FEVR);
	pk_sts->mode = !voice_mode ? NATUNE_MODE_BYPASS
		     : (voice_rsn && rsn_npu_ready()) ? NATUNE_MODE_RSN : NATUNE_MODE_PLAIN;
	pk_sts->running = s_running;
	pk_sts->rsn_ready = rsn_npu_ready() ? TRUE : FALSE;
	pk_sts->key_root = f0_key_root;
	pk_sts->scale_mask = f0_scale_mask;
	pk_sts->vibrato = f0_vibrato_ratio ? NATUNE_VIB_RATIO
			: f0_vibrato_snap_mean ? NATUNE_VIB_SNAP : NATUNE_VIB_RAW;
	pk_sts->blocks = s_blocks;
	pk_sts->hops = Voice_HopCount();
	pk_sts->misses = fifo_underruns;
	pk_sts->overruns = audio_overruns;
	pk_sts->clips = s_clips;
	F0_Get(&f0);
	pk_sts->f0_in = f0.f0_auth;
	pk_sts->f0_out = f0.f0_corr;
	pk_sts->stk_free_min = natune_stack_free_min();
	if (s_mtx > 0) tk_unl_mtx(s_mtx);
	return E_OK;
}

/*
 * npu_service.c - the task that owns the Neural-ART runtime
 *
 * Woken by the NPU completion interrupt and by the audio task whenever it
 * queues work. Runs above the other natune tasks, so a finished RSN hop is
 * synthesized and the next network started within a few microseconds.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include "npu_service.h"
#include "voice_path.h"
#include "swiftf0_npu.h"
#include "main.h"

#define NPU_EV_KICK	0x0001U

static ID s_flg;		/* 0 until started; NPU_Kick is a no-op before */
static ID s_tsk;

/* ll_aton's interrupt handler (ll_aton_platform.h maps it to this name) */
IMPORT void NPU0_IRQHandler(void);

LOCAL void npu_inthdr(UINT intno)
{
	(void)intno;
	NPU0_IRQHandler();
}

ER NPU_ServiceDefineInt(void)
{
	T_DINT dint;

	dint.intatr = TA_HLNG;
	dint.inthdr = (FP)npu_inthdr;
	return tk_def_int((UINT)NPU0_IRQn, &dint);
}

void NPU_Kick(void)
{
	if (s_flg > 0) {
		tk_set_flg(s_flg, NPU_EV_KICK);
	}
}

void NPU_Service(void)
{
	Voice_Service();		/* RSN first, it has the deadline */
	if (sf0_npu_npu_service()) {
		Voice_Service();	/* pitch tracker just freed the NPU */
	}
}

LOCAL void npu_task(INT stacd, void *exinf)
{
	UINT ptn;

	(void)stacd;
	(void)exinf;
	for (;;) {
		tk_wai_flg(s_flg, NPU_EV_KICK, TWF_ORW | TWF_BITCLR, &ptn, TMO_FEVR);
		NPU_Service();
	}
}

ER NPU_ServiceStart(PRI pri, void *stk, SZ stksz)
{
	T_CFLG cflg;
	T_CTSK ctsk;
	ER er;

	cflg.exinf = NULL;
	cflg.flgatr = TA_TFIFO | TA_WSGL;
	cflg.iflgptn = NPU_EV_KICK;	/* run once at start */
	er = tk_cre_flg(&cflg);
	if (er < E_OK) {
		return er;
	}

	ctsk.exinf = NULL;
	ctsk.tskatr = TA_HLNG | TA_RNG0 | TA_USERBUF | TA_FPU;
	ctsk.task = (FP)npu_task;
	ctsk.itskpri = pri;
	ctsk.stksz = stksz;
	ctsk.bufptr = stk;
	s_tsk = tk_cre_tsk(&ctsk);
	if (s_tsk < E_OK) {
		tk_del_flg(er);
		return s_tsk;
	}
	s_flg = er;
	return tk_sta_tsk(s_tsk, 0);
}

ID NPU_ServiceTask(void)
{
	return s_tsk;
}

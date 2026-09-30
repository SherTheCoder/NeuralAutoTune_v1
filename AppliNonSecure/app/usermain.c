/*
 * usermain.c - sample application
 *
 * Starts natune, then drops to a low priority and runs the button/LED UI.
 * usermain must not return; that would shut the kernel down.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <stdio.h>
#include "natune.h"
#include "app_ui.h"

#define APP_PRI		14	/* below the natune tasks */
#define APP_PERIOD_MS	10

EXPORT INT usermain(void)
{
	T_NATUNE_CFG cfg = {0};
	T_NATUNE_STS sts;
	ER er;

	tm_printf((UB *)"NeuralAutoTune %d.%d on micro T-Kernel %x.%02x: natune middleware sample\n",
		  NATUNE_VER_MAJOR, NATUNE_VER_MINOR, VER_MAJOR, VER_MINOR);

	AppUI_Init();

	er = natune_init(&cfg);	/* defaults: RSN, chromatic, shell on */
	if (er < E_OK) {
		tm_printf((UB *)"natune_init failed: %d\n", er);
		return 0;
	}
	er = natune_start();
	if (er < E_OK) {
		tm_printf((UB *)"natune_start failed: %d\n", er);
	}

	tk_chg_pri(TSK_SELF, APP_PRI);

	UW next = 0;
	for (;;) {
		AppUI_Poll();
		if (++next >= 1000 / APP_PERIOD_MS) {
			next = 0;
			if (natune_ref_sts(&sts) >= E_OK && !sts.running) {
				printf("App: waiting for the first corrected hop\r\n");
			}
		}
		tk_dly_tsk(APP_PERIOD_MS);
	}
	return 0;
}

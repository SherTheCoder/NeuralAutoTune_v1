/*
 * natune.h - NeuralAutoTune middleware for micro T-Kernel 3.0
 *
 * Real-time pitch correction on the STM32N6: a pitch tracker and the RSN
 * (residual synthesis network) run on the Neural-ART NPU, the rest of the
 * signal chain on the Cortex-M55.
 *
 * All functions return E_OK or a negative error code and must be called
 * from task context.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef NATUNE_H
#define NATUNE_H

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATUNE_VER_MAJOR	1
#define NATUNE_VER_MINOR	0

/* rendering modes */
#define NATUNE_MODE_BYPASS	0	/* dry voice */
#define NATUNE_MODE_PLAIN	1	/* LPC round trip, no correction */
#define NATUNE_MODE_RSN		2	/* pitch corrected */

/* vibrato handling */
#define NATUNE_VIB_RATIO	0	/* keep vibrato, centred on the note */
#define NATUNE_VIB_SNAP		1	/* flatten onto the note */
#define NATUNE_VIB_RAW		2	/* hold the sung centre */

/* scale masks, bit i = semitone i above the root */
#define NATUNE_SCALE_CHROMATIC	0x0FFFU
#define NATUNE_SCALE_MAJOR	0x0AB5U
#define NATUNE_SCALE_MINOR	0x05ADU

/* Zero-filled fields take the defaults in brackets. */
typedef struct t_natune_cfg {
	PRI	npu_pri;	/* NPU service task [2] */
	PRI	audio_pri;	/* audio task [3] */
	PRI	ctrl_pri;	/* shell/status task [12] */
	UINT	flags;		/* NATUNE_CFG_* */
	INT	mode;		/* NATUNE_MODE_*, used with NATUNE_CFG_MODE [RSN] */
	INT	key_root;	/* 0 = C .. 11 = B [C] */
	UINT	scale_mask;	/* [chromatic] */
} T_NATUNE_CFG;

#define NATUNE_CFG_NOSHELL	0x0001U	/* no serial shell */
#define NATUNE_CFG_MODE		0x0002U	/* cfg.mode is valid */
#define NATUNE_CFG_QUIET	0x0004U	/* no 1 Hz status lines */

typedef struct t_natune_sts {
	INT	mode;
	BOOL	running;	/* speaker output started */
	BOOL	rsn_ready;	/* RSN passed its self-test */
	INT	key_root;
	UINT	scale_mask;
	INT	vibrato;
	UW	blocks;		/* 1 ms audio blocks processed */
	UW	hops;		/* 5 ms synthesis hops */
	UW	misses;		/* hops late for play-out */
	UW	overruns;	/* capture blocks dropped */
	UW	clips;		/* blocks at or above -1 dBFS */
	float	f0_in;		/* Hz, 0 = unvoiced */
	float	f0_out;		/* Hz */
	UW	stk_free_min;	/* smallest task stack headroom, bytes */
} T_NATUNE_STS;

IMPORT ER natune_init(const T_NATUNE_CFG *pk_cfg);
IMPORT ER natune_start(void);
IMPORT ER natune_set_mode(INT mode);
IMPORT ER natune_set_key(INT key_root, UINT scale_mask);
IMPORT ER natune_set_vibrato(INT vib);
IMPORT ER natune_ref_sts(T_NATUNE_STS *pk_sts);

#ifdef __cplusplus
}
#endif
#endif /* NATUNE_H */

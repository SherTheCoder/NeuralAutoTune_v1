/*
 * natune_internal.h - shared between the core and the diagnostics
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef NATUNE_INTERNAL_H
#define NATUNE_INTERNAL_H

#include <stdint.h>

/* default priorities (T_NATUNE_CFG can override) and stack sizes */
#define NATUNE_NPU_PRI		2
#define NATUNE_AUDIO_PRI	3
#define NATUNE_CTRL_PRI		12

#define NATUNE_NPU_STKSZ	(6 * 1024)
#define NATUNE_AUDIO_STKSZ	(8 * 1024)
#define NATUNE_CTRL_STKSZ	(8 * 1024)

extern volatile uint32_t audio_overruns;
extern volatile uint8_t  gate_routing;

uint32_t natune_stack_free_min(void);

#endif /* NATUNE_INTERNAL_H */

/*
 * npu_service.h - the task that owns the Neural-ART runtime
 *
 * After init, every LL_ATON call happens in NPU_Service(). The RSN is served
 * before the pitch tracker because it is on the audio deadline.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef NPU_SERVICE_H
#define NPU_SERVICE_H

#include <tk/tkernel.h>

ER   NPU_ServiceDefineInt(void);			/* before the runtime init */
ER   NPU_ServiceStart(PRI pri, void *stk, SZ stksz);	/* after the self-tests */
ID   NPU_ServiceTask(void);
void NPU_Kick(void);		/* task or interrupt context */
void NPU_Service(void);

#endif /* NPU_SERVICE_H */

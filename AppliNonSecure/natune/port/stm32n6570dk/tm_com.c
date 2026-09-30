/*
 * tm_com.c - T-Monitor console on USART1
 *
 * TM_COM_SERIAL_DEV is 0 in config_tm.h, so the BSP's driver is not built and
 * kernel messages go through the same console as printf. USART1 is set up
 * by main() before the kernel starts.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "console.h"

EXPORT void tm_com_init(void)
{
}

EXPORT void tm_snd_dat(const UB *buf, INT size)
{
	Console_PutPolled((const uint8_t *)buf, (int32_t)size);
}

EXPORT void tm_rcv_dat(UB *buf, INT size)
{
	for (INT i = 0; i < size; i++) {
		buf[i] = (UB)Console_GetcPolled();
	}
}

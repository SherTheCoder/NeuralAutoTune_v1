/*
 * console.h - interrupt-driven console on USART1
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

void Console_Init(void);		/* after MX_USART1_UART_Init */
uint32_t Console_TxFree(void);
int Console_GetLine(char *line, int max);
uint32_t Console_WriteRaw(const uint8_t *p, uint32_t n);
extern volatile uint8_t console_mute;

void Console_IRQHandler(void);
void Console_PutPolled(const uint8_t *p, int32_t n);	/* T-Monitor */
int  Console_GetcPolled(void);

#endif /* CONSOLE_H */

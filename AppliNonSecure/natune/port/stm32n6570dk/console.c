/*
 * console.c - interrupt-driven console on USART1 (ST-LINK VCP)
 *
 * printf copies into a TX ring and returns; the USART interrupt drains it.
 * If the ring is full, output is dropped rather than blocking the caller.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include "console.h"
#include "main.h"

#define UART_TX_RING_SIZE 4096u            /* power of two */
#define UART_TX_RING_MASK (UART_TX_RING_SIZE - 1u)

static volatile uint8_t  uart_tx_ring[UART_TX_RING_SIZE];
static volatile uint32_t uart_tx_head = 0; /* bytes queued */
static volatile uint32_t uart_tx_tail = 0; /* bytes sent (ISR) */

#define UART_RX_RING_SIZE 1024u
#define UART_RX_RING_MASK (UART_RX_RING_SIZE - 1u)
static volatile uint8_t  uart_rx_ring[UART_RX_RING_SIZE];
static volatile uint32_t uart_rx_head = 0; /* bytes received (ISR) */
static volatile uint32_t uart_rx_tail = 0; /* bytes consumed */
volatile uint32_t uart_rx_ovr = 0;         /* hardware overruns */
volatile uint32_t uart_rx_drop = 0;        /* ring full */
volatile uint32_t uart_rx_last_ms = 0;     /* tick of the last received byte */

void Console_Init(void)
{
  /* above the NPU interrupt, or RX overruns during clip uploads */
  HAL_NVIC_SetPriority(USART1_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  SET_BIT(USART1->CR1, USART_CR1_RXNEIE_RXFNEIE);
}

/* Returns the length of a complete line (CR/LF stripped) or 0. A partial
 * line stays in `line` between calls. */
int Console_GetLine(char *line, int max)
{
  static int n = 0;
  while (uart_rx_tail != uart_rx_head)
  {
    const char c = (char)uart_rx_ring[uart_rx_tail & UART_RX_RING_MASK];
    uart_rx_tail++;
    if (c == '\r' || c == '\n')
    {
      if (n == 0) continue;
      line[n] = '\0';
      const int r = n;
      n = 0;
      return r;
    }
    if (n < max - 1) line[n++] = c;
  }
  return 0;
}

/* lets bulk output (the C1 dump) wait for room instead of being dropped */
uint32_t Console_TxFree(void)
{
  return UART_TX_RING_SIZE - (uint32_t)(uart_tx_head - uart_tx_tail);
}

/* set while "rec" streams binary audio; printf output is dropped */
volatile uint8_t console_mute = 0u;

/* queues all n bytes or none, so a stream frame is never cut */
uint32_t Console_WriteRaw(const uint8_t *p, uint32_t n)
{
  UINT imask;
  DI(imask);
  if (Console_TxFree() < n)
  {
    EI(imask);
    return 0u;
  }
  for (uint32_t i = 0; i < n; i++)
  {
    uart_tx_ring[uart_tx_head & UART_TX_RING_MASK] = p[i];
    uart_tx_head++;
  }
  SET_BIT(USART1->CR1, USART_CR1_TXEIE_TXFNFIE);
  EI(imask);
  return n;
}

int _write(int file, char *ptr, int len)
{
  (void)file;
  if (console_mute)
  {
    return len;
  }
  /* several tasks print, keep their lines whole */
  UINT imask;
  DI(imask);
  for (int i = 0; i < len; i++)
  {
    if ((uint32_t)(uart_tx_head - uart_tx_tail) >= UART_TX_RING_SIZE)
    {
      break;                               /* full: drop */
    }
    uart_tx_ring[uart_tx_head & UART_TX_RING_MASK] = (uint8_t)ptr[i];
    uart_tx_head++;
  }
  SET_BIT(USART1->CR1, USART_CR1_TXEIE_TXFNFIE);
  EI(imask);
  return len;
}

/* Called from USART1_IRQHandler before the kernel starts, and from the
 * tk_def_int handler afterwards. */
void Console_IRQHandler(void)
{
  while (READ_BIT(USART1->ISR, USART_ISR_RXNE_RXFNE))
  {
    const uint8_t c = (uint8_t)USART1->RDR;
    uart_rx_last_ms = HAL_GetTick();
    if ((uint32_t)(uart_rx_head - uart_rx_tail) < UART_RX_RING_SIZE)
    {
      uart_rx_ring[uart_rx_head & UART_RX_RING_MASK] = c;
      uart_rx_head++;
    }
    else
    {
      uart_rx_drop++;
    }
  }
  if (READ_BIT(USART1->ISR, USART_ISR_ORE))
  {
    WRITE_REG(USART1->ICR, USART_ICR_ORECF);
    uart_rx_ovr++;
  }

  if (READ_BIT(USART1->ISR, USART_ISR_TXE_TXFNF) &&
      READ_BIT(USART1->CR1, USART_CR1_TXEIE_TXFNFIE))
  {
    /* fill the whole 8-byte FIFO */
    while (uart_tx_head != uart_tx_tail && READ_BIT(USART1->ISR, USART_ISR_TXE_TXFNF))
    {
      USART1->TDR = uart_tx_ring[uart_tx_tail & UART_TX_RING_MASK];
      uart_tx_tail++;
    }
    if (uart_tx_head == uart_tx_tail)
    {
      CLEAR_BIT(USART1->CR1, USART_CR1_TXEIE_TXFNFIE);
    }
  }
}

void USART1_IRQHandler(void)
{
  Console_IRQHandler();
}

/* T-Monitor output. libtm calls this inside DI/EI, so in task context the
 * bytes are queued (the interrupt sends them after EI). From an exception
 * handler the USART is written directly. */
void Console_PutPolled(const uint8_t *p, int32_t n)
{
  if (__get_IPSR() == 0u && __get_PRIMASK() == 0u)
  {
    if (Console_TxFree() >= (uint32_t)n)
    {
      (void)Console_WriteRaw(p, (uint32_t)n);
      return;
    }
    if (__get_BASEPRI() == 0u)
    {
      while (n > 0)
      {
        const uint32_t k = (n > 64) ? 64u : (uint32_t)n;
        if (Console_WriteRaw(p, k) == k)
        {
          p += k;
          n -= (int32_t)k;
        }
      }
      return;
    }
  }
  for (uint32_t spin = 0; uart_tx_head != uart_tx_tail && spin < 2000000u; spin++)
  {
  }
  for (int32_t i = 0; i < n; i++)
  {
    while (!READ_BIT(USART1->ISR, USART_ISR_TXE_TXFNF))
    {
    }
    USART1->TDR = p[i];
  }
}

int Console_GetcPolled(void)
{
  while (uart_rx_tail == uart_rx_head)
  {
  }
  const int c = uart_rx_ring[uart_rx_tail & UART_RX_RING_MASK];
  uart_rx_tail++;
  return c;
}

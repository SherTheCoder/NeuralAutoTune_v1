/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32n6xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32n6xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Fault handlers that print the faulting PC and the fault registers by
 * polling USART1 (printf is not safe here). Used until the kernel starts;
 * after that the kernel's own handlers take over. */
static void fault_putc(char c)
{
  while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0u) { }
  USART1->TDR = (uint32_t)(uint8_t)c;
}
static void fault_tag(const char *s) { while (*s) fault_putc(*s++); }
static void fault_hex(uint32_t v)
{
  fault_putc('0'); fault_putc('x');
  for (int i = 28; i >= 0; i -= 4)
    fault_putc("0123456789ABCDEF"[(v >> i) & 0xFu]);
}

/* also kept in memory for the debugger */
volatile uint32_t g_fault_type, g_fault_pc, g_fault_lr, g_fault_psr;
volatile uint32_t g_fault_cfsr, g_fault_hfsr, g_fault_mmfar, g_fault_bfar, g_fault_exclr;

/* frame: stacked R0-R3, R12, LR, PC, xPSR. type: 1 mem, 2 bus, 3 usage, 4 hard */
void Fault_Report(uint32_t *frame, uint32_t exc_lr, uint32_t type);
void Fault_Report(uint32_t *frame, uint32_t exc_lr, uint32_t type)
{
  g_fault_type  = type;        g_fault_pc   = frame[6]; g_fault_lr = frame[5];
  g_fault_psr   = frame[7];    g_fault_cfsr = SCB->CFSR; g_fault_hfsr = SCB->HFSR;
  g_fault_mmfar = SCB->MMFAR;  g_fault_bfar = SCB->BFAR; g_fault_exclr = exc_lr;

  static const char *const names[] = { "?", "MEMMANAGE", "BUS", "USAGE", "HARD" };
  fault_tag("\r\n*** FAULT(");
  fault_tag(names[type <= 4u ? type : 0u]);
  fault_tag(")  PC=");                    fault_hex(frame[6]);
  fault_tag("  LR=");                     fault_hex(frame[5]);
  fault_tag("  xPSR=");                   fault_hex(frame[7]);
  fault_tag("\r\n              CFSR=");   fault_hex(SCB->CFSR);
  fault_tag("  HFSR=");                   fault_hex(SCB->HFSR);
  fault_tag("  MMFAR=");                  fault_hex(SCB->MMFAR);
  fault_tag("  BFAR=");                   fault_hex(SCB->BFAR);
  fault_tag("  EXC_RETURN=");             fault_hex(exc_lr);
  fault_tag("\r\n");
  while (1) { }
}

/* Hard and bus faults may be taken by the secure FSBL instead. */
__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile (
    "tst  lr, #4        \n"   /* which stack */
    "ite  eq            \n"
    "mrseq r0, msp      \n"
    "mrsne r0, psp      \n"
    "mov  r1, lr        \n"
    "movs r2, #4        \n"
    "b    Fault_Report  \n"
  );
}
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_NodeTypeDef Node_GPDMA1_Channel0;
extern DMA_QListTypeDef List_GPDMA1_Channel0;
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern DMA_NodeTypeDef Node_GPDMA1_Channel1;
extern DMA_QListTypeDef List_GPDMA1_Channel1;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Memory management fault.
  */
/* USER CODE BEGIN MemoryManagement_IRQn 0 */
__attribute__((naked)) void MemManage_Handler(void)
{
  __asm volatile (
    "tst  lr, #4 \n" "ite eq \n" "mrseq r0, msp \n" "mrsne r0, psp \n"
    "mov  r1, lr \n" "movs r2, #1 \n" "b Fault_Report \n");   /* type = MEM */
}
__attribute__((naked)) void BusFault_Handler(void)
{
  __asm volatile (
    "tst  lr, #4 \n" "ite eq \n" "mrseq r0, msp \n" "mrsne r0, psp \n"
    "mov  r1, lr \n" "movs r2, #2 \n" "b Fault_Report \n");   /* type = BUS */
}
__attribute__((naked)) void UsageFault_Handler(void)
{
  __asm volatile (
    "tst  lr, #4 \n" "ite eq \n" "mrseq r0, msp \n" "mrsne r0, psp \n"
    "mov  r1, lr \n" "movs r2, #3 \n" "b Fault_Report \n");   /* type = USAGE */
}
/* USER CODE END MemoryManagement_IRQn 0 */

/**
  * @brief This function handles Secure fault.
  */
void SecureFault_Handler(void)
{
  /* USER CODE BEGIN SecureFault_IRQn 0 */

  /* USER CODE END SecureFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_SecureFault_IRQn 0 */
    /* USER CODE END W1_SecureFault_IRQn 0 */
  }
}

/* micro T-Kernel 3.0 owns SVCall, PendSV (its dispatcher) and SysTick (its
 * system timer): knl_start_mtkernel() copies this vector table to RAM and
 * installs its own handlers there. SysTick_Handler below only serves the HAL
 * time base BEFORE the kernel starts (main(): clock and peripheral init). */

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32N6xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32n6xx.s).                    */
/******************************************************************************/

/* GPDMA1 channel 0/1 (audio capture/playback) and USART1 (console) are
 * defined with tk_def_int by the natune port (natune_port.c); NPU0 by the
 * natune NPU service (npu_service.c). */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/*
 * natune_port.c - STM32N6570-DK port: audio DMA, WM8904 codec, interrupt
 * definitions and the HAL time base
 *
 * Handlers that call the kernel must sit at priority 1..14; the kernel masks
 * priority 1 and up in its critical sections, so 0 is not used.
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include <stdio.h>
#include "main.h"
#include "natune_port.h"
#include "console.h"

#define BUFFER_SIZE	(2U * NATUNE_BLOCK)

#define PRI_AUDIO_DMA	1
#define PRI_CONSOLE	3

/* Non-cacheable (MPU region 0). The MDF result is 24-bit right-aligned in a
 * 32-bit word, so the mic buffer is int32_t. */
__attribute__((section(".non_cacheable_data"))) static int32_t mic_buf[BUFFER_SIZE];
__attribute__((section(".non_cacheable_data"))) static int16_t spk_buf[BUFFER_SIZE];

/* from Core/Src/main.c */
IMPORT MDF_HandleTypeDef MdfHandle0;
IMPORT MDF_FilterConfigTypeDef MdfFilterConfig0;
IMPORT DMA_HandleTypeDef handle_GPDMA1_Channel0;
IMPORT DMA_HandleTypeDef handle_GPDMA1_Channel1;
IMPORT SAI_HandleTypeDef hsai_BlockA1;

/* the codec is on I2C2 (PD14/PD4), not I2C1 */
static I2C_HandleTypeDef hi2c2;

int32_t *natune_port_mic_half(UINT h) { return &mic_buf[h ? NATUNE_BLOCK : 0U]; }
int16_t *natune_port_spk_half(UINT h) { return &spk_buf[h ? NATUNE_BLOCK : 0U]; }

BOOL natune_port_tx_collision(const int16_t *spk)
{
	const uint32_t sai_idx = (handle_GPDMA1_Channel1.Instance->CSAR
				  - (uint32_t)spk_buf) >> 1;
	return (sai_idx < BUFFER_SIZE &&
		(sai_idx >= NATUNE_BLOCK) == (spk >= &spk_buf[NATUNE_BLOCK])) ? TRUE : FALSE;
}

LOCAL void gpdma_ch0_inthdr(UINT intno)
{
	(void)intno;
	HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
}

LOCAL void gpdma_ch1_inthdr(UINT intno)
{
	(void)intno;
	HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
}

LOCAL void usart1_inthdr(UINT intno)
{
	(void)intno;
	Console_IRQHandler();
}

void HAL_MDF_AcqHalfCpltCallback(MDF_HandleTypeDef *hmdf)
{
	(void)hmdf;
	natune_isr_block(&mic_buf[0], 0U);
}

void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf)
{
	(void)hmdf;
	natune_isr_block(&mic_buf[NATUNE_BLOCK], 1U);
}

/* A DMA start can return HAL_OK and still fail on its first bus access, so
 * report the errors. dma_err: 1 bus, 2 link fetch, 4 user setting, 8 trigger */
void HAL_MDF_ErrorCallback(MDF_HandleTypeDef *hmdf)
{
	printf("MDF error: mdf_err=0x%08lx dma_err=0x%04lx\r\n",
	       (unsigned long)hmdf->ErrorCode,
	       (hmdf->hdma != NULL) ? (unsigned long)hmdf->hdma->ErrorCode : 0UL);
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
	printf("SAI error: sai_err=0x%08lx dma_err=0x%04lx\r\n",
	       (unsigned long)hsai->ErrorCode,
	       (hsai->hdmatx != NULL) ? (unsigned long)hsai->hdmatx->ErrorCode : 0UL);
}

LOCAL ER def_int(UINT intno, FP hdr, INT pri)
{
	T_DINT dint;
	ER er;

	dint.intatr = TA_HLNG;
	dint.inthdr = hdr;
	er = tk_def_int(intno, &dint);
	if (er >= E_OK) {
		EnableInt(intno, pri);
	}
	return er;
}

/* HAL time base: a 1 ms cyclic handler once the kernel owns SysTick */
LOCAL volatile BOOL s_kernel_tick;

LOCAL void hal_tick_cychdr(void *exinf)
{
	(void)exinf;
	HAL_IncTick();
}

/* HAL's own SysTick is only used before the kernel starts */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
	if (s_kernel_tick) {
		return HAL_OK;
	}
	if (SysTick_Config(SystemCoreClock / (1000U / (uint32_t)uwTickFreq)) != 0U) {
		return HAL_ERROR;
	}
	if (TickPriority < (1UL << __NVIC_PRIO_BITS)) {
		HAL_NVIC_SetPriority(SysTick_IRQn, TickPriority, 0U);
		uwTickPrio = TickPriority;
	}
	return HAL_OK;
}

/* sleep when called from a task, busy-wait otherwise */
void HAL_Delay(uint32_t Delay)
{
	if (s_kernel_tick && __get_IPSR() == 0U) {
		tk_dly_tsk((TMO)((Delay == 0U) ? 1U : Delay));
		return;
	}
	const uint32_t t0 = HAL_GetTick();
	uint32_t wait = Delay;
	if (wait < HAL_MAX_DELAY) {
		wait += (uint32_t)uwTickFreq;
	}
	while ((HAL_GetTick() - t0) < wait) {
	}
}

ER natune_port_kernel_init(void)
{
	T_CCYC ccyc;
	ER er;

	ccyc.exinf = NULL;
	ccyc.cycatr = TA_HLNG | TA_STA | TA_PHS;
	ccyc.cychdr = (FP)hal_tick_cychdr;
	ccyc.cyctim = 1;
	ccyc.cycphs = 1;
	er = tk_cre_cyc(&ccyc);
	if (er < E_OK) {
		return er;
	}
	s_kernel_tick = TRUE;

	er = def_int((UINT)USART1_IRQn, (FP)usart1_inthdr, PRI_CONSOLE);
	if (er < E_OK) {
		return er;
	}
	er = def_int((UINT)GPDMA1_Channel0_IRQn, (FP)gpdma_ch0_inthdr, PRI_AUDIO_DMA);
	if (er < E_OK) {
		return er;
	}
	return def_int((UINT)GPDMA1_Channel1_IRQn, (FP)gpdma_ch1_inthdr, PRI_AUDIO_DMA);
}

/* WM8904 bring-up, register sequence from ST's wm8904.c driver */
ER natune_port_codec_init(void)
{
	/* release reset (PB1, active low) */
	GPIO_InitTypeDef rst = {0};
	rst.Pin = GPIO_PIN_1;
	rst.Mode = GPIO_MODE_OUTPUT_PP;
	rst.Pull = GPIO_NOPULL;
	rst.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &rst);
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
	HAL_Delay(10);

	/* I2C2: PD14 SCL, PD4 SDA, ~100 kHz */
	RCC_PeriphCLKInitTypeDef i2c2clk = {0};
	i2c2clk.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
	i2c2clk.I2c2ClockSelection = RCC_I2C2CLKSOURCE_PCLK1;
	if (HAL_RCCEx_PeriphCLKConfig(&i2c2clk) != HAL_OK) {
		return E_IO;
	}
	__HAL_RCC_GPIOD_CLK_ENABLE();
	GPIO_InitTypeDef i2c = {0};
	i2c.Pin = GPIO_PIN_14 | GPIO_PIN_4;
	i2c.Mode = GPIO_MODE_AF_OD;
	i2c.Pull = GPIO_NOPULL;
	i2c.Speed = GPIO_SPEED_FREQ_HIGH;
	i2c.Alternate = GPIO_AF4_I2C2;
	HAL_GPIO_Init(GPIOD, &i2c);
	__HAL_RCC_I2C2_CLK_ENABLE();
	hi2c2.Instance = I2C2;
	hi2c2.Init.Timing = 0x30C0EDFF;
	hi2c2.Init.OwnAddress1 = 0;
	hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c2.Init.OwnAddress2 = 0;
	hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
	if (HAL_I2C_Init(&hi2c2) != HAL_OK) {
		return E_IO;
	}

	/* board revisions have either a WM8904 or a CS42L51 */
	uint8_t wm_id[2] = {0};
	HAL_StatusTypeDef probe = HAL_I2C_Mem_Read(&hi2c2, 0x34, 0x00, I2C_MEMADD_SIZE_8BIT, wm_id, 2, 100);
	printf("Codec probe WM8904 @0x34: st=%d id=0x%02X%02X (expect st=0 id=0x8904)\r\n",
	       probe, wm_id[0], wm_id[1]);
	uint8_t cs_id = 0;
	probe = HAL_I2C_Mem_Read(&hi2c2, 0x94, 0x01, I2C_MEMADD_SIZE_8BIT, &cs_id, 1, 100);
	printf("Codec probe CS42L51 @0x94: st=%d id=0x%02X (expect st=0 id=0xD9)\r\n",
	       probe, cs_id);

	/* 48 kHz, I2S 16-bit, headphone out */
	static const uint16_t cfg[][2] = {
		{0x00, 0x0000}, /* SW reset                        */
		{0x16, 0x0004}, /* Clock Rates 2: SYSCLK from MCLK */
		{0x04, 0x0018}, /* Bias Control 0                  */
		{0x05, 0x0047}, /* VMID Control 0: fast start-up   */
		{0x05, 0x0043}, /* VMID Control 0: normal          */
		{0x04, 0x0019}, /* Bias Control 0: enable          */
		{0x0E, 0x0003}, /* PWR MGMT 2: HPL/HPR PGA enable  */
		{0x16, 0x0006}, /* Clock Rates 2: + DSP clock      */
		{0x12, 0x000C}, /* PWR MGMT 6: DACL + DACR enable  */
		{0x21, 0x0648}, /* DAC Digital 1: 48 kHz (muted)   */
		{0x18, 0x0010}, /* Audio Interface 0               */
		{0x19, 0x0002}, /* Audio Interface 1: I2S 16-bit   */
		{0x15, 0x0C05}, /* Clock Rates 1: 48 kHz, MCLK=256fs */
		{0x62, 0x0001}, /* Charge Pump 0: enable           */
		{0x5A, 0x0011}, /* Analog HP 0: HPL/HPR enable     */
		{0x5A, 0x0033}, /* Analog HP 0: + ENA_DLY          */
		{0x43, 0x0003}, /* DC Servo 0: enable L/R          */
		{0x44, 0x0030}, /* DC Servo 1: start (polled below) */
		{0x5A, 0x0077}, /* Analog HP 0: + ENA_OUTP         */
		{0x5A, 0x00FF}, /* Analog HP 0: + RMV_SHORT        */
		{0x68, 0x0001}, /* Class W 0: dynamic power        */
		{0x39, 0x0039}, /* HP-L volume: 0 dB (low 6 bits, ~1 dB/step) */
		{0x3A, 0x00B9}, /* HP-R volume: 0 dB + VU latch    */
		{0x21, 0x0640}, /* DAC Digital 1: UNMUTE           */
	};
	uint32_t nacks = 0;
	for (uint32_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
		uint8_t b[3] = { (uint8_t)cfg[i][0], (uint8_t)(cfg[i][1] >> 8), (uint8_t)cfg[i][1] };
		if (HAL_I2C_Master_Transmit(&hi2c2, 0x34, b, 3, 100) != HAL_OK) {
			nacks++;
		}
		if (cfg[i][0] == 0x00) {
			HAL_Delay(10); /* let the soft reset settle */
		}
		if (cfg[i][0] == 0x44) {
			/* wait up to 300 ms for the DC servo */
			uint8_t rb[2] = {0};
			for (uint32_t t = 0; t < 30U; t++) {
				HAL_Delay(10);
				if ((HAL_I2C_Mem_Read(&hi2c2, 0x34, 0x4D, I2C_MEMADD_SIZE_8BIT, rb, 2, 100) == HAL_OK)
				    && ((rb[1] & 0x03U) == 0x03U)) {
					break;
				}
			}
			printf("WM8904 DC servo readback: 0x%02X%02X (bits1:0 = cal done)\r\n", rb[0], rb[1]);
		}
	}
	printf("WM8904 init: %lu/%u writes failed\r\n",
	       (unsigned long)nacks, (unsigned)(sizeof(cfg) / sizeof(cfg[0])));
	return (nacks == 0U) ? E_OK : E_IO;
}

ER natune_port_start_capture(void)
{
	/* the NPU runtime sleeps in WFE; keep these clocks running in sleep */
	__HAL_RCC_GPDMA1_CLK_SLEEP_ENABLE();
	__HAL_RCC_MDF1_CLK_SLEEP_ENABLE();
	__HAL_RCC_SAI1_CLK_SLEEP_ENABLE();
	__HAL_RCC_USART1_CLK_SLEEP_ENABLE();
	__HAL_RCC_FLEXRAM_MEM_CLK_SLEEP_ENABLE();
	__HAL_RCC_AXISRAM1_MEM_CLK_SLEEP_ENABLE();
	__HAL_RCC_AXISRAM2_MEM_CLK_SLEEP_ENABLE();

	MDF_DmaConfigTypeDef dma = {0};
	dma.Address    = (uint32_t)mic_buf;
	dma.DataLength = BUFFER_SIZE * sizeof(int32_t);	/* bytes */
	dma.MsbOnly    = DISABLE;

	HAL_StatusTypeDef st = HAL_MDF_AcqStart_DMA(&MdfHandle0, &MdfFilterConfig0, &dma);
	if (st != HAL_OK) {
		printf("MDF DMA start failed: status=%d mdf_state=%d dma_state=%d\r\n",
		       st, MdfHandle0.State, handle_GPDMA1_Channel0.State);
		return E_IO;
	}
	printf("MDF DMA started (playback deferred until first FIFO write)\r\n");
	return E_OK;
}

ER natune_port_start_playback(void)
{
	HAL_StatusTypeDef st = HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)spk_buf, BUFFER_SIZE);
	if (st != HAL_OK) {
		printf("SAI DMA start failed: status=%d sai_state=0x%02x sai_err=0x%08lx dma_state=%d dma_err=0x%04lx\r\n",
		       st, hsai_BlockA1.State, (unsigned long)hsai_BlockA1.ErrorCode,
		       handle_GPDMA1_Channel1.State, (unsigned long)handle_GPDMA1_Channel1.ErrorCode);
		return E_IO;
	}
	printf("SAI DMA started (after first FIFO write: deterministic prefill)\r\n");
	return E_OK;
}

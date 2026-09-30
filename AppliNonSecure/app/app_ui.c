/*
 * app_ui.c - USER1 button and LEDs, using only the natune API
 *
 * USER1 short press: next key/scale
 * USER1 long press:  correction on/off
 * LD1 green: on = correcting, blinking = bypass, off = RSN not available
 * LD2 red:   flashes on clipping or a late hop
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include <stdio.h>
#include "natune.h"
#include "app_ui.h"
#include "main.h"

#define BTN_PORT   GPIOC
#define BTN_PIN    GPIO_PIN_13          /* active high */
#define LDG_PORT   GPIOO
#define LDG_PIN    GPIO_PIN_1           /* active high */
#define LDR_PORT   GPIOG
#define LDR_PIN    GPIO_PIN_10          /* active low */

#define DEBOUNCE_MS  25u
#define LONG_MS      800u
#define FLASH_MS     100u

typedef struct { const char *name; INT root; UINT mask; } preset_t;

/* chromatic is the power-on default */
static const preset_t k_presets[] = {
  { "chromatic", 0, NATUNE_SCALE_CHROMATIC },
  { "C major",   0, NATUNE_SCALE_MAJOR }, { "G major", 7, NATUNE_SCALE_MAJOR },
  { "D major",   2, NATUNE_SCALE_MAJOR }, { "A major", 9, NATUNE_SCALE_MAJOR },
  { "E major",   4, NATUNE_SCALE_MAJOR }, { "F major", 5, NATUNE_SCALE_MAJOR },
  { "Bb major", 10, NATUNE_SCALE_MAJOR }, { "Eb major", 3, NATUNE_SCALE_MAJOR },
  { "A minor",   9, NATUNE_SCALE_MINOR }, { "E minor", 4, NATUNE_SCALE_MINOR },
  { "D minor",   2, NATUNE_SCALE_MINOR }, { "C minor", 0, NATUNE_SCALE_MINOR },
};
#define N_PRESETS (sizeof k_presets / sizeof k_presets[0])

static uint32_t s_preset;
static uint8_t  s_btn_state, s_btn_raw, s_long_done;
static uint32_t s_btn_t_change, s_btn_t_down;
static uint32_t s_flash_until;
static UW       s_last_miss, s_last_clip;
static INT      s_last_mode = NATUNE_MODE_RSN;

static void led_green(uint8_t on) { HAL_GPIO_WritePin(LDG_PORT, LDG_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static void led_red(uint8_t on)   { HAL_GPIO_WritePin(LDR_PORT, LDR_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET); }

static void apply_preset(uint32_t i)
{
  natune_set_key(k_presets[i].root, k_presets[i].mask);
  printf("UI: key/scale -> %s (root %d, mask %03x)\r\n", k_presets[i].name,
         (int)k_presets[i].root, (unsigned)k_presets[i].mask);
}

void AppUI_Init(void)
{
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();         /* GPIOO is clocked by the FSBL */
  g.Pin = BTN_PIN; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLDOWN; g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BTN_PORT, &g);
  g.Pin = LDG_PIN; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(LDG_PORT, &g);
  g.Pin = LDR_PIN;
  HAL_GPIO_Init(LDR_PORT, &g);
  led_red(0);
  led_green(0);
  s_preset = 0u;
  printf("UI: USER1 short = next key/scale, long = bypass; LD1 green = correcting, LD2 red = clip/miss\r\n");
}

void AppUI_Poll(void)
{
  T_NATUNE_STS sts;
  const uint32_t now = HAL_GetTick();

  if (natune_ref_sts(&sts) < E_OK)
  {
    return;
  }

  /* button */
  const uint8_t raw = (HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN) == GPIO_PIN_SET) ? 1u : 0u;
  if (raw != s_btn_raw) { s_btn_raw = raw; s_btn_t_change = now; }
  if (raw != s_btn_state && (now - s_btn_t_change) >= DEBOUNCE_MS)
  {
    s_btn_state = raw;
    if (raw)
    {
      s_btn_t_down = now;
      s_long_done = 0u;
    }
    else if (!s_long_done)
    {
      s_preset = (s_preset + 1u) % N_PRESETS;
      apply_preset(s_preset);
    }
  }
  if (s_btn_state && !s_long_done && (now - s_btn_t_down) >= LONG_MS)
  {
    s_long_done = 1u;
    if (sts.mode == NATUNE_MODE_BYPASS)
    {
      natune_set_mode(s_last_mode);
      printf("UI: pitch correction ON\r\n");
    }
    else
    {
      s_last_mode = sts.mode;
      natune_set_mode(NATUNE_MODE_BYPASS);
      printf("UI: BYPASS (dry voice)\r\n");
    }
  }

  /* LEDs */
  if (!sts.rsn_ready)                     led_green(0);
  else if (sts.mode != NATUNE_MODE_BYPASS) led_green(1);
  else                                    led_green(((now / 500u) & 1u) != 0u);

  if (sts.misses != s_last_miss || sts.clips != s_last_clip)
  {
    s_last_miss = sts.misses;
    s_last_clip = sts.clips;
    s_flash_until = now + FLASH_MS;
  }
  led_red((int32_t)(s_flash_until - now) > 0);
}

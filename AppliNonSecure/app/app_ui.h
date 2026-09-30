/*
 * app_ui.h - USER1 button and LEDs
 *
 * Copyright 2026 Sher Amir Singh Dullat
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_UI_H
#define APP_UI_H

void AppUI_Init(void);
void AppUI_Poll(void);          /* call every 10 ms */

#endif /* APP_UI_H */

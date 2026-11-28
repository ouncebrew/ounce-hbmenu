/* SPDX-License-Identifier: ISC
 * Switch 2 HOME Menu UI for ounce-hbmenu.
 *
 * Based on ImDella/Switch2-HBmenu-Home, used with credit to its author.
 */
#ifndef HBMENU_SWITCH2_UI_H
#define HBMENU_SWITCH2_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "../common/common.h"

void switch2UiInit(ThemePreset system_preset);
void switch2MenuDraw(void);
bool switch2MenuUpdate(u64 down);
/* Currently held pad buttons; lets the UI detect "still held at the edge". */
void switch2SetHeldButtons(u64 held);
void switch2HandleTouch(menu_s *menu);
void switch2HandlePointer(int x, int y, bool left_click, bool right_click,
                          bool moved);

#endif

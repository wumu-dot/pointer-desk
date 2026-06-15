/**
 * settings_mode.h — 设置菜单模式
 */

#ifndef SETTINGS_MODE_H
#define SETTINGS_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "button.h"

void settings_mode_init(void);
void settings_mode_enter(void);
void settings_mode_exit(void);
void settings_mode_update(void);
void settings_mode_render(void);
void settings_mode_handle_button(button_id_t btn, button_event_t event);

#endif /* SETTINGS_MODE_H */

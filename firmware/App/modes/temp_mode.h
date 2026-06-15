/**
 * temp_mode.h — 温度计模式
 */

#ifndef TEMP_MODE_H
#define TEMP_MODE_H

#include <stdbool.h>
#include "button.h"

void temp_mode_init(void);
void temp_mode_enter(void);
void temp_mode_exit(void);
void temp_mode_update(void);       /* 每5秒调用 */
void temp_mode_render(void);
void temp_mode_handle_button(button_id_t btn, button_event_t event);
bool temp_mode_is_fahrenheit(void);

#endif /* TEMP_MODE_H */

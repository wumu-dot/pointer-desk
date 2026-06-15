/**
 * clock_mode.h — 时钟模式
 */

#ifndef CLOCK_MODE_H
#define CLOCK_MODE_H

#include <stdbool.h>
#include "button.h"

void clock_mode_init(void);
void clock_mode_enter(void);
void clock_mode_exit(void);
void clock_mode_update(void);      /* 每秒调用 */
void clock_mode_render(void);      /* 发送渲染指令 */
void clock_mode_handle_button(button_id_t btn, button_event_t event);
bool clock_mode_use_24h(void);

#endif /* CLOCK_MODE_H */

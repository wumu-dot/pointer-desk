/**
 * timer_mode.h — 计时器模式
 */

#ifndef TIMER_MODE_H
#define TIMER_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "button.h"

void timer_mode_init(void);
void timer_mode_enter(void);
void timer_mode_exit(void);
void timer_mode_update(void);       /* 每秒调用 */
void timer_mode_render(void);
void timer_mode_handle_button(button_id_t btn, button_event_t event);
bool timer_mode_is_running(void);
uint32_t timer_mode_get_remaining_sec(void);
uint32_t timer_mode_get_total_sec(void);

#endif /* TIMER_MODE_H */

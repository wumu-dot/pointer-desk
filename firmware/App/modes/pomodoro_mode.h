/**
 * pomodoro_mode.h — 番茄钟模式
 */

#ifndef POMODORO_MODE_H
#define POMODORO_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "button.h"

void pomodoro_mode_init(void);
void pomodoro_mode_enter(void);
void pomodoro_mode_exit(void);
void pomodoro_mode_update(void);
void pomodoro_mode_render(void);
void pomodoro_mode_handle_button(button_id_t btn, button_event_t event);

#endif /* POMODORO_MODE_H */

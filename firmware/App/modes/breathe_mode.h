/**
 * breathe_mode.h — 呼吸引导模式 (4-7-8 呼吸法)
 */

#ifndef BREATHE_MODE_H
#define BREATHE_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "button.h"

void breathe_mode_init(void);
void breathe_mode_enter(void);
void breathe_mode_exit(void);
void breathe_mode_update(void);
void breathe_mode_render(void);
void breathe_mode_handle_button(button_id_t btn, button_event_t event);

#endif /* BREATHE_MODE_H */

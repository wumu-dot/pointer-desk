/**
 * mode_manager.h — 模式状态机
 *
 * 管理模式切换和各模式的入口函数。
 */

#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "button.h"

/* ================================================================
 * 模式 ID
 * ================================================================ */
typedef enum {
    MODE_CLOCK = 0,
    MODE_TEMP,
    MODE_TIMER,
    MODE_SETTINGS,
    MODE_COUNT            /* 模式总数 */
} mode_id_t;

/* ================================================================
 * 模式状态
 * ================================================================ */
typedef enum {
    APP_STATE_STARTUP,          /* 启动, 检查 RTC */
    APP_STATE_TIME_SETUP,       /* 首次设置时间 */
    APP_STATE_RUNNING,          /* 正常运行 */
} app_state_t;

/* ================================================================
 * API
 * ================================================================ */
void mode_manager_init(void);

/* 模式切换 */
void mode_manager_switch_to(mode_id_t mode);
mode_id_t mode_manager_get_current(void);

/* 按键分发 */
void mode_manager_handle_button(button_id_t btn, button_event_t event);

/* 各模式的周期更新 (后台任务调用) */
void mode_manager_update(void);

/* 渲染请求 (后台任务调用，发送到显示任务队列) */
void mode_manager_render(void);

/* 应用状态 */
app_state_t mode_manager_get_app_state(void);
void mode_manager_set_app_state(app_state_t state);

#endif /* MODE_MANAGER_H */

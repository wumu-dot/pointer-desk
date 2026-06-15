/**
 * mode_manager.c — 模式状态机
 *
 * 管理 4 个操作模式 (时钟/温度/计时器/设置) 和应用启动状态。
 * 使用函数指针分发表避免长 if/else 链。
 */

#include "mode_manager.h"

#include "modes/clock_mode.h"
#include "modes/temp_mode.h"
#include "modes/timer_mode.h"
#include "modes/settings_mode.h"

#include "rtc_drv.h"
#include "gui.h"
#include "st7735.h"
#include "app_config.h"

/* ================================================================
 * 模式操作分发表
 * ================================================================ */
typedef struct {
    void (*init)(void);
    void (*enter)(void);
    void (*exit)(void);
    void (*update)(void);
    void (*render)(void);
    void (*button)(button_id_t, button_event_t);
    const char *name;
} mode_ops_t;

static const mode_ops_t mode_ops[MODE_COUNT] = {
    [MODE_CLOCK]    = {
        clock_mode_init,   clock_mode_enter,   clock_mode_exit,
        clock_mode_update, clock_mode_render,  clock_mode_handle_button,
        "Clock"
    },
    [MODE_TEMP]     = {
        temp_mode_init,    temp_mode_enter,    temp_mode_exit,
        temp_mode_update,  temp_mode_render,   temp_mode_handle_button,
        "Temp"
    },
    [MODE_TIMER]    = {
        timer_mode_init,   timer_mode_enter,   timer_mode_exit,
        timer_mode_update, timer_mode_render,  timer_mode_handle_button,
        "Timer"
    },
    [MODE_SETTINGS] = {
        settings_mode_init,   settings_mode_enter,   settings_mode_exit,
        settings_mode_update, settings_mode_render,  settings_mode_handle_button,
        "Settings"
    },
};

/* ================================================================
 * 静态状态
 * ================================================================ */
static app_state_t  app_state    = APP_STATE_STARTUP;
static mode_id_t    current_mode = MODE_CLOCK;

/* ================================================================
 * 公开 API
 * ================================================================ */

void mode_manager_init(void)
{
    /* 初始化所有模式 */
    for (mode_id_t i = 0; i < MODE_COUNT; i++) {
        if (mode_ops[i].init != NULL) {
            mode_ops[i].init();
        }
    }

    /* 初始化 RTC */
    rtc_drv_init();

    /* 检查 RTC 有效性，决定初始应用状态 */
    if (!rtc_drv_is_valid()) {
        app_state = APP_STATE_TIME_SETUP;
        LOG("RTC not set — entering time setup flow");
    } else {
        app_state = APP_STATE_RUNNING;
    }

    /* 进入默认模式: 时钟 */
    current_mode = MODE_CLOCK;
    if (mode_ops[MODE_CLOCK].enter != NULL) {
        mode_ops[MODE_CLOCK].enter();
    }

    LOG("Mode manager initialized: app_state=%d, mode=%s",
        app_state, mode_ops[current_mode].name);
}

void mode_manager_switch_to(mode_id_t mode)
{
    if (mode >= MODE_COUNT) {
        return;
    }

    /* 同模式且运行态下无需切换 */
    if (mode == current_mode && app_state == APP_STATE_RUNNING) {
        return;
    }

    /* 退出当前模式 */
    if (mode_ops[current_mode].exit != NULL) {
        mode_ops[current_mode].exit();
    }

    const char *old_name = mode_ops[current_mode].name;

    /* 切换并进入新模式 */
    current_mode = mode;
    if (mode_ops[current_mode].enter != NULL) {
        mode_ops[current_mode].enter();
    }

    LOG("Mode switch: %s -> %s", old_name, mode_ops[current_mode].name);
}

mode_id_t mode_manager_get_current(void)
{
    return current_mode;
}

void mode_manager_handle_button(button_id_t btn, button_event_t event)
{
    /* 忽略无效事件 */
    if (event == BTN_EVENT_NONE) {
        return;
    }

    /* 时间设置模式: 任意按键跳转设置模式配置时间 */
    if (app_state == APP_STATE_TIME_SETUP) {
        app_state = APP_STATE_RUNNING;
        mode_manager_switch_to(MODE_SETTINGS);
        return;
    }

    /* 全局导航: 左右键短按切换模式 (循环) */
    if (event == BTN_EVENT_SHORT_PRESS) {
        if (btn == BTN_LEFT) {
            mode_id_t prev = (current_mode == 0)
                           ? (MODE_COUNT - 1)
                           : (current_mode - 1);
            mode_manager_switch_to(prev);
            return;
        }
        if (btn == BTN_RIGHT) {
            mode_id_t next = (current_mode + 1) % MODE_COUNT;
            mode_manager_switch_to(next);
            return;
        }
    }

    /* 分发给当前模式处理 */
    if (mode_ops[current_mode].button != NULL) {
        mode_ops[current_mode].button(btn, event);
    }
}

void mode_manager_update(void)
{
    if (mode_ops[current_mode].update != NULL) {
        mode_ops[current_mode].update();
    }
}

void mode_manager_render(void)
{
    if (mode_ops[current_mode].render != NULL) {
        mode_ops[current_mode].render();
    }
}

app_state_t mode_manager_get_app_state(void)
{
    return app_state;
}

void mode_manager_set_app_state(app_state_t state)
{
    app_state = state;
    LOG("App state changed to: %d", state);
}

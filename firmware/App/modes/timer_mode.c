/**
 * timer_mode.c — 倒计时模式
 *
 * 提供倒计时器功能：
 * - 进度环显示剩余时间比例
 * - TFT 屏幕视觉反馈（结束时闪烁替代电机震动）
 * - 支持开始/暂停/重置/调整时长
 */

#include "timer_mode.h"
#include "gui.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

/* ================================================================
 * 计时器状态
 * ================================================================ */
typedef enum {
    TIMER_STOPPED,
    TIMER_RUNNING,
    TIMER_PAUSED,
    TIMER_FINISHED,
} timer_state_t;

/* ================================================================
 * 静态变量
 * ================================================================ */
static timer_state_t state;
static uint32_t      total_sec;
static uint32_t      remaining_sec;
static uint32_t      start_tick;       /* HAL_GetTick() 计时起点 */

/* 渲染去重缓存 */
static struct {
    uint32_t      remaining;
    uint32_t      total;
    timer_state_t st;
} render_cache = { 0xFFFFFFFF, 0, TIMER_STOPPED };

/* ================================================================
 * 内部辅助
 * ================================================================ */
static void timer_refresh_display(void) {
    /* 清屏并标记全屏脏，触发 render 重绘 */
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void timer_mode_init(void) {
    total_sec     = TIMER_DEFAULT_MINUTES * 60;
    remaining_sec = total_sec;
    state         = TIMER_STOPPED;
    start_tick    = 0;
}

void timer_mode_enter(void) {
    if (state == TIMER_STOPPED) {
        remaining_sec = total_sec;
    }
    /* 重置渲染缓存，确保进入时强制重绘 */
    render_cache.remaining = 0xFFFFFFFF;
    render_cache.st        = TIMER_STOPPED;
    timer_refresh_display();
}

void timer_mode_exit(void) {
    if (state == TIMER_RUNNING) {
        /* 离开时自动暂停，防止后台继续计时 */
        state = TIMER_PAUSED;
        LOG("Timer auto-paused on exit");
    }
}

void timer_mode_update(void) {
    if (state == TIMER_RUNNING) {
        uint32_t now     = HAL_GetTick();
        uint32_t elapsed = (now - start_tick) / 1000;

        if (elapsed >= total_sec) {
            remaining_sec = 0;
            state         = TIMER_FINISHED;

            /* TFT 闪烁替代电机震动 */
            for (uint8_t i = 0; i < TIMER_END_VIBRATE_COUNT; i++) {
                st7735_fill_screen(COLOR_WHITE);
                HAL_Delay(150);
                st7735_fill_screen(COLOR_BLACK);
                HAL_Delay(150);
            }
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            LOG("Timer finished!");
        } else {
            remaining_sec = total_sec - elapsed;
        }
    }
}

void timer_mode_render(void) {
    uint16_t color;
    float    ratio;
    int16_t  end_deg;
    uint32_t mins, secs;
    char     time_str[16];
    const char *status_str;

    /* 去重: 剩余秒数/总时长/状态均无变化则跳过渲染 */
    if (remaining_sec == render_cache.remaining
        && total_sec == render_cache.total
        && state == render_cache.st) {
        return;
    }
    render_cache.remaining = remaining_sec;
    render_cache.total     = total_sec;
    render_cache.st        = state;

    /* ---- 进度环颜色 ---- */
    ratio = 1.0f - (float)remaining_sec / (float)total_sec;
    end_deg = (int16_t)(ratio * 360.0f);

    if (state == TIMER_FINISHED) {
        color = COLOR_YELLOW;
    } else if (remaining_sec <= TIMER_URGENT_SECONDS && state == TIMER_RUNNING) {
        color = COLOR_RED;
    } else {
        color = COLOR_GREEN;
    }

    /* ---- 背景环 (灰色整圈) ---- */
    gui_draw_arc(64, 55, 40, 0, 359, 4, COLOR_DARK_GRAY);

    /* ---- 前景环 (已走完的部分) ---- */
    if (end_deg > 0) {
        gui_draw_arc(64, 55, 40, 0, end_deg, 4, color);
    }

    /* ---- 时间数字 ---- */
    mins = remaining_sec / 60;
    secs = remaining_sec % 60;
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu",
             (unsigned long)mins, (unsigned long)secs);
    gui_draw_text_centered(64, 55, time_str, 3, color, COLOR_BLACK);

    /* ---- 状态标签 ---- */
    switch (state) {
        case TIMER_STOPPED:  status_str = "STOP";  break;
        case TIMER_RUNNING:  status_str = "RUN";   break;
        case TIMER_PAUSED:   status_str = "PAUSE"; break;
        case TIMER_FINISHED: status_str = "DONE!"; break;
        default:             status_str = "";      break;
    }
    gui_draw_text_centered(64, 100, status_str, 1,
                           (state == TIMER_FINISHED) ? COLOR_YELLOW : COLOR_GRAY,
                           COLOR_BLACK);

    /* ---- 底部提示 ---- */
    gui_draw_text_aligned(150, "RIGHT:start/stop  UP/DOWN:mode",
                          0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);
}

void timer_mode_handle_button(button_id_t btn, button_event_t event) {

    /* RIGHT 短按 = 开始/暂停 */
    if ((btn == BTN_RIGHT || btn == BTN_CENTER) && event == BTN_EVENT_SHORT_PRESS) {
        if (state == TIMER_FINISHED) {
            state = TIMER_STOPPED;
            remaining_sec = total_sec;
            LOG("Timer reset from FINISHED");
        } else if (state == TIMER_RUNNING) {
            state = TIMER_PAUSED;
            LOG("Timer paused");
        } else {
            if (state == TIMER_PAUSED) {
                start_tick = HAL_GetTick() - (total_sec - remaining_sec) * 1000;
            } else {
                remaining_sec = total_sec;
                start_tick    = HAL_GetTick();
            }
            state = TIMER_RUNNING;
            LOG("Timer started (remaining=%lu sec)", (unsigned long)remaining_sec);
        }
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        return;
    }

    /* LONG = 重置 */
    if (event == BTN_EVENT_LONG_PRESS) {
        state = TIMER_STOPPED;
        remaining_sec = total_sec;
        LOG("Timer reset");
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    }
}

bool timer_mode_is_running(void) {
    return (state == TIMER_RUNNING);
}

uint32_t timer_mode_get_remaining_sec(void) {
    return remaining_sec;
}

uint32_t timer_mode_get_total_sec(void) {
    return total_sec;
}

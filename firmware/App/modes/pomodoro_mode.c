/**
 * pomodoro_mode.c — 番茄钟模式
 *
 * 25分钟专注 + 5分钟休息，自动循环。
 * 进度环显示当前阶段剩余时间比例，配色区分工作/休息。
 * 结束时 TFT 闪烁提示，自动进入下一阶段。
 */

#include "pomodoro_mode.h"
#include "gui.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

/* ================================================================
 * 常量
 * ================================================================ */
#define POMODORO_WORK_MIN  25
#define POMODORO_BREAK_MIN 5
#define POMODORO_WORK_SEC  (POMODORO_WORK_MIN * 60)
#define POMODORO_BREAK_SEC (POMODORO_BREAK_MIN * 60)

/* ================================================================
 * 阶段
 * ================================================================ */
typedef enum {
    PHASE_WORK,
    PHASE_BREAK,
    PHASE_FINISHED,
} pomo_phase_t;

/* ================================================================
 * 静态变量
 * ================================================================ */
static pomo_phase_t phase;
static uint32_t     total_sec;
static uint32_t     remaining_sec;
static uint32_t     start_tick;
static uint8_t      completed_sessions;

static struct {
    uint32_t     remaining;
    uint32_t     total;
    pomo_phase_t ph;
    uint8_t      sessions;
} render_cache = { 0xFFFFFFFF, 0, PHASE_WORK, 0xFF };

/* ================================================================
 * 公开 API
 * ================================================================ */

void pomodoro_mode_init(void) {
    total_sec          = POMODORO_WORK_SEC;
    remaining_sec      = total_sec;
    phase              = PHASE_WORK;
    start_tick         = 0;
    completed_sessions = 0;
}

void pomodoro_mode_enter(void) {
    /* 重置渲染缓存，确保重绘 */
    render_cache.remaining = 0xFFFFFFFF;
    render_cache.sessions  = 0xFF;
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    LOG("POMODORO: enter, phase=%d sessions=%u", phase, completed_sessions);
}

void pomodoro_mode_exit(void) {
    LOG("POMODORO: exit, sessions=%u", completed_sessions);
}

void pomodoro_mode_update(void) {
    if (phase == PHASE_FINISHED) return;

    uint32_t now     = HAL_GetTick();
    uint32_t elapsed = (now - start_tick) / 1000;

    if (elapsed >= total_sec) {
        remaining_sec = 0;

        /* Flash screen */
        for (uint8_t i = 0; i < 2; i++) {
            st7735_fill_screen(COLOR_WHITE);
            HAL_Delay(150);
            st7735_fill_screen(COLOR_BLACK);
            HAL_Delay(150);
        }

        /* Auto transition */
        if (phase == PHASE_WORK) {
            completed_sessions++;
            total_sec     = POMODORO_BREAK_SEC;
            phase         = PHASE_BREAK;
            remaining_sec = total_sec;
            start_tick    = HAL_GetTick();
            LOG("POMODORO: work done -> break. sessions=%u", completed_sessions);
        } else {
            total_sec     = POMODORO_WORK_SEC;
            phase         = PHASE_WORK;
            remaining_sec = total_sec;
            start_tick    = HAL_GetTick();
            LOG("POMODORO: break done -> work. sessions=%u", completed_sessions);
        }

        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    } else {
        remaining_sec = total_sec - elapsed;
    }
}

void pomodoro_mode_render(void) {
    if (remaining_sec == render_cache.remaining
        && total_sec == render_cache.total
        && phase == render_cache.ph
        && completed_sessions == render_cache.sessions) {
        return;
    }
    render_cache.remaining = remaining_sec;
    render_cache.total     = total_sec;
    render_cache.ph        = phase;
    render_cache.sessions  = completed_sessions;

    uint16_t color, ring_color;
    float    ratio;
    int16_t  end_deg;
    uint32_t mins, secs;
    char     time_str[16], session_str[16];
    const char *phase_str;

    /* Theme: work = red/orange, break = green/cyan */
    if (phase == PHASE_WORK) {
        ring_color = COLOR_ORANGE;
        color      = (remaining_sec <= 60) ? COLOR_RED : COLOR_ORANGE;
        phase_str  = "FOCUS";
    } else {
        ring_color = COLOR_GREEN;
        color      = COLOR_CYAN;
        phase_str  = "BREAK";
    }

    /* ---- Progress ring ---- */
    ratio   = 1.0f - (float)remaining_sec / (float)total_sec;
    end_deg = (int16_t)(ratio * 360.0f);

    gui_draw_arc(64, 50, 38, 0, 359, 4, COLOR_DARK_GRAY);
    if (end_deg > 0) {
        gui_draw_arc(64, 50, 38, 0, end_deg, 4, ring_color);
    }

    /* ---- Time ---- */
    mins = remaining_sec / 60;
    secs = remaining_sec % 60;
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu",
             (unsigned long)mins, (unsigned long)secs);
    gui_draw_text_centered(64, 50, time_str, 3, color, COLOR_BLACK);

    /* ---- Phase label ---- */
    gui_draw_text_centered(64, 95, phase_str, 1, ring_color, COLOR_BLACK);

    /* ---- Session count ---- */
    snprintf(session_str, sizeof(session_str), "x%u", completed_sessions);
    gui_draw_text_centered(64, 115, session_str, 0,
                           (completed_sessions > 0) ? COLOR_WHITE : COLOR_DARK_GRAY,
                           COLOR_BLACK);

    /* ---- Hint ---- */
    gui_draw_text_aligned(150, "RIGHT:start/pause  LONG:reset",
                          0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);
}

void pomodoro_mode_handle_button(button_id_t btn, button_event_t event) {

    /* RIGHT 短按 = 启动/暂停 */
    if ((btn == BTN_RIGHT || btn == BTN_CENTER) && event == BTN_EVENT_SHORT_PRESS) {
        if (start_tick == 0) {
            start_tick = HAL_GetTick();
            LOG("POMODORO: started");
        } else {
            start_tick = 0;
            LOG("POMODORO: paused (start_tick=0)");
        }
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        return;
    }

    /* LONG = 重置 */
    if (event == BTN_EVENT_LONG_PRESS) {
        total_sec          = POMODORO_WORK_SEC;
        remaining_sec      = total_sec;
        phase              = PHASE_WORK;
        completed_sessions = 0;
        start_tick         = 0;
        LOG("POMODORO: reset");
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    }
}

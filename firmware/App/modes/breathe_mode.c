/**
 * breathe_mode.c — 呼吸引导模式 (4-7-8 呼吸法)
 *
 * 圆圈动画引导缓慢呼吸：
 *   INHALE  (4s): 圆圈从小逐渐变大
 *   HOLD    (7s): 圆圈保持最大
 *   EXHALE  (8s): 圆圈从大逐渐变小
 *
 * 纯 TFT 视觉反馈，无需电机。长按退出。
 */

#include "breathe_mode.h"
#include "gui.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

/* ================================================================
 * 4-7-8 呼吸节奏 (秒)
 * ================================================================ */
#define BREATHE_INHALE_S  4
#define BREATHE_HOLD_S    7
#define BREATHE_EXHALE_S  8

/* Circle geometry */
#define RING_CX         64
#define RING_CY         70
#define RING_R_MAX      40
#define RING_R_MIN      5
#define RING_THICKNESS  6

/* ================================================================
 * 阶段
 * ================================================================ */
typedef enum {
    BREATHE_INHALE,
    BREATHE_HOLD,
    BREATHE_EXHALE,
} breathe_phase_t;

/* ================================================================
 * 静态变量
 * ================================================================ */
static breathe_phase_t phase;
static uint32_t        phase_start_tick;
static bool             running;

static uint16_t        last_r          = 0xFFFF;  /* dedup: last drawn radius */
static breathe_phase_t last_phase      = -1;
static uint32_t        last_sec_remain = 0xFFFFFFFF;

/* ================================================================
 * Helpers
 * ================================================================ */

static uint32_t phase_duration(breathe_phase_t p) {
    switch (p) {
        case BREATHE_INHALE: return BREATHE_INHALE_S * 1000;
        case BREATHE_HOLD:   return BREATHE_HOLD_S   * 1000;
        case BREATHE_EXHALE: return BREATHE_EXHALE_S * 1000;
        default:             return 1000;
    }
}

/* ================================================================
 * 公开 API
 * ================================================================ */

void breathe_mode_init(void) {
    phase      = BREATHE_INHALE;
    running    = false;
}

void breathe_mode_enter(void) {
    running          = true;
    phase            = BREATHE_INHALE;
    phase_start_tick = HAL_GetTick();
    last_r           = 0xFFFF;
    last_phase       = -1;
    last_sec_remain  = 0xFFFFFFFF;
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    LOG("BREATHE: enter");
}

void breathe_mode_exit(void) {
    running = false;
    LOG("BREATHE: exit");
}

void breathe_mode_update(void) {
    if (!running) return;

    uint32_t now     = HAL_GetTick();
    uint32_t elapsed = now - phase_start_tick;

    if (elapsed >= phase_duration(phase)) {
        switch (phase) {
            case BREATHE_INHALE:
                phase = BREATHE_HOLD;
                LOG("BREATHE: -> HOLD");
                break;
            case BREATHE_HOLD:
                phase = BREATHE_EXHALE;
                LOG("BREATHE: -> EXHALE");
                break;
            case BREATHE_EXHALE:
                phase = BREATHE_INHALE;
                LOG("BREATHE: -> INHALE");
                break;
        }
        phase_start_tick = now;
    }
}

void breathe_mode_render(void) {
    if (!running) return;

    uint32_t elapsed  = HAL_GetTick() - phase_start_tick;
    uint32_t dur      = phase_duration(phase);
    float    progress = (dur > 0) ? (float)elapsed / (float)dur : 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    uint16_t r, color;
    const char *label;

    switch (phase) {
        case BREATHE_INHALE:
            r     = (uint16_t)(RING_R_MIN + (float)(RING_R_MAX - RING_R_MIN) * progress);
            color = COLOR_CYAN;
            label = "INHALE";
            break;
        case BREATHE_HOLD:
            r     = RING_R_MAX;
            color = COLOR_GREEN;
            label = "HOLD";
            break;
        case BREATHE_EXHALE:
            r     = (uint16_t)(RING_R_MIN + (float)(RING_R_MAX - RING_R_MIN) * (1.0f - progress));
            color = COLOR_ORANGE;
            label = "EXHALE";
            break;
        default:
            r = RING_R_MIN; color = COLOR_WHITE; label = "";
            break;
    }

    uint32_t sec_remain = (dur - elapsed) / 1000 + 1;

    /* ---- Dedup: only redraw if r/phase/sec changed ---- */
    if (r == last_r && phase == last_phase && sec_remain == last_sec_remain) {
        return;
    }
    last_r          = r;
    last_phase      = phase;
    last_sec_remain = sec_remain;

    /* ---- Erase ring area with black ---- */
    uint16_t erase_x = RING_CX - RING_R_MAX - RING_THICKNESS;
    uint16_t erase_y = RING_CY - RING_R_MAX - RING_THICKNESS;
    uint16_t erase_s = (RING_R_MAX + RING_THICKNESS) * 2;
    st7735_fill_rect(erase_x, erase_y, erase_s, erase_s, COLOR_BLACK);
    gui_dirty_mark(erase_x, erase_y, erase_s, erase_s);

    /* ---- Draw ring at current radius (0-359 avoids arc normalize bug) ---- */
    gui_draw_arc(RING_CX, RING_CY, r, 0, 359, RING_THICKNESS, color);

    /* ---- Phase label above ring ---- */
    gui_draw_text_centered(RING_CX, RING_CY - RING_R_MAX - 12, label,
                           1, color, COLOR_BLACK);

    /* ---- Countdown seconds below ring ---- */
    char hint[8];
    snprintf(hint, sizeof(hint), "%lu\"", (unsigned long)sec_remain);
    gui_draw_text_centered(RING_CX, RING_CY + RING_R_MAX + 12, hint,
                           0, COLOR_GRAY, COLOR_BLACK);

    /* ---- Bottom hint ---- */
    gui_draw_text_aligned(155, "RIGHT:start/stop  LEFT:back",
                          0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);
}

void breathe_mode_handle_button(button_id_t btn, button_event_t event) {

    /* RIGHT 短按 = 开始/暂停呼吸 */
    if ((btn == BTN_RIGHT || btn == BTN_CENTER) && event == BTN_EVENT_SHORT_PRESS) {
        if (running) {
            running = false;
            LOG("BREATHE: paused");
        } else {
            running = true;
            phase_start_tick = HAL_GetTick();
            phase = BREATHE_INHALE;
            last_r = 0xFFFF;
            last_phase = -1;
            LOG("BREATHE: started");
        }
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        return;
    }

    /* LONG = 退出呼吸 */
    if (event == BTN_EVENT_LONG_PRESS) {
        running = false;
        LOG("BREATHE: user exit");
    }
}

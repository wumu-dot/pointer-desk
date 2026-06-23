/**
 * breathe_mode.c — 呼吸引导模式 (4-7-8 呼吸法)
 *
 * 圆圈动画引导缓慢呼吸：
 *   INHALE  (4s): 圆圈从小逐渐变大
 *   HOLD    (7s): 圆圈保持最大，轻微脉冲
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
#define BREATHE_CYCLE_S   (BREATHE_INHALE_S + BREATHE_HOLD_S + BREATHE_EXHALE_S)  /* 19s */

/* Circle geometry */
#define RING_CX         64
#define RING_CY         70
#define RING_R_MAX      45
#define RING_R_MIN      8
#define RING_THICKNESS  5

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

/* Render dedup */
static struct {
    uint16_t        r;
    uint16_t        color;
    breathe_phase_t ph;
    bool            running;
} render_cache = { 0, 0, BREATHE_INHALE, false };

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
    LOG("BREATHE: init");
}

void breathe_mode_enter(void) {
    running          = true;
    phase            = BREATHE_INHALE;
    phase_start_tick = HAL_GetTick();
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
        /* Transition to next phase */
        switch (phase) {
            case BREATHE_INHALE: phase = BREATHE_HOLD;   break;
            case BREATHE_HOLD:   phase = BREATHE_EXHALE; break;
            case BREATHE_EXHALE: phase = BREATHE_INHALE; break;
        }
        phase_start_tick = now;
        elapsed = 0;
    }
}

void breathe_mode_render(void) {
    if (!running) return;

    uint32_t elapsed = HAL_GetTick() - phase_start_tick;
    uint32_t dur     = phase_duration(phase);
    float    progress = (dur > 0) ? (float)elapsed / (float)dur : 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    uint16_t r, color;
    const char *label;

    switch (phase) {
        case BREATHE_INHALE:
            r     = (uint16_t)(RING_R_MIN + (RING_R_MAX - RING_R_MIN) * progress);
            color = COLOR_CYAN;
            label = "INHALE";
            break;
        case BREATHE_HOLD:
            r     = RING_R_MAX;  /* steady at max */
            color = COLOR_GREEN;
            label = "HOLD";
            break;
        case BREATHE_EXHALE:
            r     = (uint16_t)(RING_R_MAX - (RING_R_MAX - RING_R_MIN) * progress);
            color = COLOR_ORANGE;
            label = "EXHALE";
            break;
        default:
            r = RING_R_MIN; color = COLOR_WHITE; label = "";
            break;
    }

    /* Dedup */
    if (r == render_cache.r && color == render_cache.color
        && phase == render_cache.ph && running == render_cache.running) {
        return;
    }
    render_cache.r       = r;
    render_cache.color   = color;
    render_cache.ph      = phase;
    render_cache.running = running;

    /* ---- Draw animated circle ---- */
    /* Erase previous */
    st7735_fill_rect(RING_CX - RING_R_MAX - RING_THICKNESS,
                     RING_CY - RING_R_MAX - RING_THICKNESS,
                     (RING_R_MAX + RING_THICKNESS) * 2,
                     (RING_R_MAX + RING_THICKNESS) * 2,
                     COLOR_BLACK);
    gui_dirty_mark(RING_CX - RING_R_MAX - RING_THICKNESS,
                   RING_CY - RING_R_MAX - RING_THICKNESS,
                   (RING_R_MAX + RING_THICKNESS) * 2,
                   (RING_R_MAX + RING_THICKNESS) * 2);

    gui_draw_arc(RING_CX, RING_CY, r, 0, 360, RING_THICKNESS, color);

    /* ---- Phase label ---- */
    gui_draw_text_centered(RING_CX, RING_CY, label, 1, color, COLOR_BLACK);

    /* ---- Time progress for this phase ---- */
    uint32_t sec_remain = (dur - elapsed) / 1000 + 1;
    char hint[16];
    snprintf(hint, sizeof(hint), "%lu\"", (unsigned long)sec_remain);
    gui_draw_text_centered(RING_CX, 140, hint, 0, COLOR_GRAY, COLOR_BLACK);

    /* ---- Bottom hint ---- */
    gui_draw_text_aligned(155, "HOLD: exit",
                          0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);
}

void breathe_mode_handle_button(button_id_t btn, button_event_t event) {
    (void)btn;
    /* Long press exits breathe mode (mode_manager auto-switches to next mode) */
    if (event == BTN_EVENT_LONG_PRESS) {
        running = false;
        LOG("BREATHE: user exit");
    }
}

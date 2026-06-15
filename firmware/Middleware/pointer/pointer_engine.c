/**
 * pointer_engine.c — Pointer Engine
 *
 * Pure computation module for the physical stepper-motor-driven pointer.
 * Handles:
 *   - Smooth interpolation (current → target angle) with shortest-path logic
 *   - Multiple movement modes with configurable step factors
 *   - Scale-aware angle mapping for clock, temperature, timer, and page displays
 *
 * No hardware dependencies. Call pointer_engine_update() every 50 ms from the
 * motor task; all setter functions are safe to call from any task context.
 */

#include "pointer_engine.h"
#include "app_config.h"
#include "main.h"

#include <math.h>
#include <string.h>

/* ===================================================================
 * Module-level state (single static instance)
 * =================================================================== */
static pointer_state_t state;

/* ===================================================================
 * Forward declarations — internal helpers
 * =================================================================== */

/**
 * @brief Compute the shortest signed angular difference from `from` to `to`.
 *        Result is always in the range (-180, 180].
 */
static float angle_diff(float from, float to)
{
    float d = to - from;

    while (d > 180.0f) {
        d -= 360.0f;
    }
    while (d < -180.0f) {
        d += 360.0f;
    }

    return d;
}

/**
 * @brief Normalize an angle into [0, 360).
 */
static float normalize_angle(float angle)
{
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    while (angle < 0.0f) {
        angle += 360.0f;
    }
    return angle;
}

/**
 * @brief Clamp a value between min and max.
 */
static float clampf(float value, float min_val, float max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
 * @brief Return the interpolation factor for a given move mode.
 */
static float move_mode_factor(pointer_move_mode_t mode)
{
    switch (mode) {
        case POINTER_MOVE_SMOOTH:
            return 1.0f / (float)POINTER_SMOOTH_STEPS;   /* 1/20 — 1 s to target */
        case POINTER_MOVE_NORMAL:
            return 1.0f / 10.0f;                          /* 1/10 */
        case POINTER_MOVE_FAST:
            return 1.0f / 4.0f;                           /* 1/4 */
        case POINTER_MOVE_URGENT:
            return 1.0f;                                  /* instant jump */
        default:
            return 1.0f / 10.0f;
    }
}

/* ===================================================================
 * Public API
 * =================================================================== */

/**
 * @brief Initialise the pointer engine to a known zero state.
 */
void pointer_engine_init(void)
{
    memset(&state, 0, sizeof(state));

    state.current_angle = 0.0f;
    state.target_angle  = 0.0f;
    state.move_mode     = POINTER_MOVE_SMOOTH;
    state.is_moving     = false;
    state.last_update_ms = 0;

    LOG("Pointer engine initialised");
}

/**
 * @brief Set an absolute target angle (0-360 deg) with an explicit move mode.
 * @note  The angle is normalised into [0, 360) before being stored.
 */
void pointer_set_target(float angle, pointer_move_mode_t mode)
{
    float new_target = normalize_angle(angle);

    /* 仅在目标角度或移动模式变化时才记录日志 */
    bool angle_changed = (fabsf(angle_diff(state.target_angle, new_target)) > 0.05f);
    bool mode_changed  = (state.move_mode != mode);

    state.target_angle = new_target;
    state.move_mode    = mode;
    state.is_moving    = true;

    if (angle_changed || mode_changed) {
        LOG("Pointer target set: %.1f deg (mode %d)", (double)state.target_angle, mode);
    }
}

/* ---------------------------------------------------------------
 * Scale-aware setters — each translates domain values to an angle
 * and delegates to pointer_set_target().
 * --------------------------------------------------------------- */

/**
 * @brief 12-hour clock mode.
 *        hour range: 0-11 (modulo 12 applied internally).
 *        angle = (hour % 12) * 30 + minute * 0.5
 */
void pointer_set_clock(uint8_t hour, uint8_t minute)
{
    float h = (float)(hour % 12);
    float m = (float)minute;
    float angle = h * 30.0f + m * 0.5f;


    pointer_set_target(angle, POINTER_MOVE_SMOOTH);
}

/**
 * @brief 24-hour clock mode.
 *        hour range: 0-23 (modulo 24 applied internally).
 *        angle = (hour % 24) * 15 + minute * 0.25
 */
void pointer_set_clock_24h(uint8_t hour, uint8_t minute)
{
    float h = (float)(hour % 24);
    float m = (float)minute;
    float angle = h * 15.0f + m * 0.25f;


    pointer_set_target(angle, POINTER_MOVE_SMOOTH);
}

/**
 * @brief Temperature gauge mode.
 *
 *        Maps the temperature onto a 300-degree arc from 30 deg to 330 deg.
 *        Celsius range:    -10 .. 50  deg C
 *        Fahrenheit range:  14 .. 122 deg F
 *
 *        The input value is clamped to the range, then linearly interpolated:
 *            angle = 30 + (temp - min) / (max - min) * 300
 */
void pointer_set_temperature(float temp_c, bool fahrenheit)
{
    float min_val, max_val;

    if (fahrenheit) {
        min_val = 14.0f;
        max_val = 122.0f;
    } else {
        min_val = -10.0f;
        max_val = 50.0f;
    }

    float temp = clampf(temp_c, min_val, max_val);
    float angle = 30.0f + (temp - min_val) / (max_val - min_val) * 300.0f;

    pointer_set_target(angle, POINTER_MOVE_NORMAL);
}

/**
 * @brief Timer countdown mode.
 *
 *        The pointer represents elapsed time on a full 360-degree sweep.
 *        0 degrees = full (nothing elapsed), 360 degrees = empty (all elapsed).
 *
 *        angle = (1 - remaining / total) * 360
 *
 *        The final 10 seconds use URGENT mode; otherwise NORMAL.
 */
void pointer_set_timer(uint32_t remaining_sec, uint32_t total_sec)
{
    if (total_sec == 0) {
        pointer_set_target(0.0f, POINTER_MOVE_NORMAL);
        return;
    }

    // Guard against remaining exceeding total (should never happen).
    if (remaining_sec > total_sec) {
        remaining_sec = total_sec;
    }

    float fraction = (float)remaining_sec / (float)total_sec;
    float angle = (1.0f - fraction) * 360.0f;   /* 0 deg = full, 360 deg = empty */

    pointer_move_mode_t mode = (remaining_sec <= TIMER_URGENT_SECONDS)
                                   ? POINTER_MOVE_URGENT
                                   : POINTER_MOVE_NORMAL;


    pointer_set_target(angle, mode);
}

/**
 * @brief Settings page indicator.
 *
 *        Maps the current page number onto a 240-degree arc from 60 deg to 300 deg.
 *        angle = 60 + page / (total_pages - 1) * 240
 *
 *        If total_pages <= 1 the pointer stays at 60 deg (single page).
 */
void pointer_set_page(uint8_t page, uint8_t total_pages)
{
    if (total_pages <= 1) {
        pointer_set_target(60.0f, POINTER_MOVE_FAST);
        return;
    }

    if (page >= total_pages) {
        page = (uint8_t)(total_pages - 1);
    }

    float angle = 60.0f + (float)page / (float)(total_pages - 1) * 240.0f;

    pointer_set_target(angle, POINTER_MOVE_FAST);
}

/**
 * @brief Per-tick update — call every 50 ms (POINTER_UPDATE_MS).
 *
 *        Moves current_angle toward target_angle along the shortest angular path
 *        using a step factor that depends on the active move mode.
 *
 *        When the remaining difference drops below 0.1 degrees the pointer snaps
 *        to target and is_moving is cleared.
 */
void pointer_engine_update(void)
{
    uint32_t now = HAL_GetTick();

    /* Throttle: enforce POINTER_UPDATE_MS period */
    if ((now - state.last_update_ms) < POINTER_UPDATE_MS) {
        return;
    }
    state.last_update_ms = now;

    if (!state.is_moving) {
        return;
    }

    float diff = angle_diff(state.current_angle, state.target_angle);

    /* Already at target (within tolerance) */
    if (diff == 0.0f) {
        state.is_moving = false;
        return;
    }

    float factor = move_mode_factor(state.move_mode);
    float step   = diff * factor;

    state.current_angle += step;

    /* Re-normalise after applying step */
    state.current_angle = normalize_angle(state.current_angle);

    /* Check if we have converged */
    float remaining = angle_diff(state.current_angle, state.target_angle);

    if (remaining < 0.1f && remaining > -0.1f) {
        state.current_angle = state.target_angle;
        state.is_moving = false;
        LOG("Pointer reached target: %.1f deg", (double)state.target_angle);
    }
}

/* ---------------------------------------------------------------
 * State query functions
 * --------------------------------------------------------------- */

pointer_state_t pointer_get_state(void)
{
    return state;
}

bool pointer_has_reached_target(void)
{
    return !state.is_moving;
}

float pointer_get_current_angle(void)
{
    return state.current_angle;
}

/**
 * @brief Software zeroing — resets the internal angle to 0 degrees.
 *
 *        In production a calibration sequence would drive the motor into a
 *        mechanical end-stop and then call this to synchronise software state.
 */
void pointer_calibrate(void)
{
    state.current_angle = 0.0f;
    state.target_angle  = 0.0f;
    state.is_moving     = false;

    LOG("Pointer calibrated to zero");
}

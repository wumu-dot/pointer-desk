/**
 * pointer_engine.h — 指针引擎
 *
 * 负责物理指针的位置管理：
 * - 平滑插值 (current → target)
 * - 梯形加减速 (加速 → 匀速 → 减速)
 * - 不同模式的刻度映射
 */

#ifndef POINTER_ENGINE_H
#define POINTER_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 运动模式
 * ================================================================ */
typedef enum {
    POINTER_MOVE_SMOOTH,    /* 缓慢平滑 (时钟模式) */
    POINTER_MOVE_NORMAL,    /* 普通速度 (温度模式) */
    POINTER_MOVE_FAST,      /* 快速切换 (模式切换) */
    POINTER_MOVE_URGENT,    /* 紧急跳步 (计时器最后10秒) */
} pointer_move_mode_t;

/* ================================================================
 * 刻度映射
 * ================================================================ */
typedef enum {
    SCALE_CLOCK,            /* 12等分 (时钟) */
    SCALE_TEMP_C,           /* -10~50°C, 300°弧 */
    SCALE_TEMP_F,           /* 14~122°F, 300°弧 */
    SCALE_TIMER_60,         /* 60分钟一圈 */
    SCALE_PAGE,             /* 页码 (设置菜单, 最多8页) */
} pointer_scale_t;

/* ================================================================
 * 指针状态
 * ================================================================ */
typedef struct {
    float    current_angle;     /* 当前角度 (0-360°) */
    float    target_angle;      /* 目标角度 */
    pointer_move_mode_t move_mode;
    pointer_scale_t scale;
    bool     is_moving;
    uint32_t last_update_ms;
} pointer_state_t;

/* ================================================================
 * API
 * ================================================================ */
void pointer_engine_init(void);

/* 设置目标角度 (绝对角度 0-360°) */
void pointer_set_target(float angle, pointer_move_mode_t mode);

/* 按刻度设置目标 (自动计算角度) */
void pointer_set_clock(uint8_t hour, uint8_t minute);           /* 12h 时钟 */
void pointer_set_clock_24h(uint8_t hour, uint8_t minute);       /* 24h 时钟 */
void pointer_set_temperature(float temp_c, bool fahrenheit);    /* 温度计 */
void pointer_set_timer(uint32_t remaining_sec, uint32_t total_sec); /* 计时器 */
void pointer_set_page(uint8_t page, uint8_t total_pages);      /* 设置菜单页码 */

/* 每个 POINTER_UPDATE_MS 周期调用一次 (50ms)，由电机任务调用 */
void pointer_engine_update(void);

/* 状态查询 */
pointer_state_t pointer_get_state(void);
bool pointer_has_reached_target(void);
float pointer_get_current_angle(void);

/* 归零校准 (移动到0°机械限位) */
void pointer_calibrate(void);

#endif /* POINTER_ENGINE_H */

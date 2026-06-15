/**
 * gui.h — 轻量GUI 绘图库
 *
 * 提供比 st7735 更高层次的绘图 API：
 * - 脏矩形追踪 (只刷新变化区域)
 * - 画布缓冲 (可选)
 * - 高级图形 (进度环、弧形刻度盘等)
 */

#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 脏矩形追踪
 * ================================================================ */
typedef struct {
    uint16_t x, y, w, h;
    bool     valid;
} dirty_rect_t;

#define MAX_DIRTY_RECTS     8

void gui_dirty_mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void gui_dirty_clear(void);
uint8_t gui_dirty_get_count(void);
const dirty_rect_t* gui_dirty_get_all(void);

/* 合并所有脏矩形为一个最小包围矩形 */
dirty_rect_t gui_dirty_merge(void);

/* ================================================================
 * 初始化
 * ================================================================ */
void gui_init(void);

/* ================================================================
 * 高级图形 (内部会标记脏矩形)
 * ================================================================ */

/* 弧形进度条 (从角度 start 到 end，顺时针)
 * 用于计时器进度环、温度刻度盘等 */
void gui_draw_arc(uint16_t cx, uint16_t cy, uint16_t r,
                  int16_t start_deg, int16_t end_deg,
                  uint16_t thickness, uint16_t color);

/* 刻度线 (弧形排列的短线段)
 * 用于温度计刻度盘、时钟刻度等 */
void gui_draw_tick_marks(uint16_t cx, uint16_t cy, uint16_t r,
                         int16_t start_deg, int16_t end_deg,
                         uint8_t count, uint16_t color);

/* 数字仪表 (大号居中数字 + 小号单位)
 * 用于温度显示、计时器显示等 */
void gui_draw_meter(uint16_t cx, uint16_t cy,
                    const char *value, const char *unit,
                    uint16_t color_val, uint16_t color_unit);

/* 图标 (预设小图标: 太阳/月亮/箭头等) */
typedef enum {
    ICON_SUN,
    ICON_MOON,
    ICON_ARROW_UP,
    ICON_ARROW_DOWN,
    ICON_ARROW_LEFT,
    ICON_ARROW_RIGHT,
    ICON_CHECK,
    ICON_CROSS,
} gui_icon_t;

void gui_draw_icon(uint16_t x, uint16_t y, gui_icon_t icon, uint16_t color);

/* ================================================================
 * 布局辅助
 * ================================================================ */

/* 获取文本尺寸 */
void gui_text_size(const char *str, uint8_t font_id, uint16_t *w, uint16_t *h);

/* 居中绘制文本 */
void gui_draw_text_centered(uint16_t cx, uint16_t cy,
                            const char *str, uint8_t font_id,
                            uint16_t fg, uint16_t bg);

/* 对齐绘制 */
typedef enum { GUI_ALIGN_LEFT, GUI_ALIGN_CENTER, GUI_ALIGN_RIGHT } gui_align_t;
void gui_draw_text_aligned(uint16_t y, const char *str, uint8_t font_id,
                           uint16_t fg, uint16_t bg, gui_align_t align);

#endif /* GUI_H */

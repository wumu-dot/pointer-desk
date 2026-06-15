/**
 * gui.c — Lightweight GUI library
 *
 * Implements dirty-rectangle tracking and high-level drawing primitives
 * on top of the st7735 LCD driver.
 *
 * Dependencies:
 *   - st7735.h  → st7735_draw_pixel, st7735_draw_line, st7735_fill_rect,
 *                  st7735_draw_text, st7735_draw_char
 *   - pin_config.h → LCD_WIDTH, LCD_HEIGHT
 *   - app_config.h → LOG macro
 *   - <math.h>   → cosf, sinf (link with -lm)
 *   - <string.h> → memset, strlen
 */

#include "gui.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ================================================================
 * 内部 —— 脏矩形追踪状态
 * ================================================================ */
static dirty_rect_t dirty_rects[MAX_DIRTY_RECTS];

/* ================================================================
 * 内部 —— 字体映射
 * ================================================================ */

/**
 * 将外部 font_id (0-3) 映射到 st7735 的 font_size_t 枚举
 *
 *   0 → FONT_6x8    (6×8 px)
 *   1 → FONT_8x16   (8×16 px)
 *   2 → FONT_12x24  (12×24 px)
 *   3 → FONT_16x32  (16×32 px)
 */
static font_size_t gui_map_font(uint8_t font_id) {
    switch (font_id) {
        case 0: return FONT_6x8;
        case 1: return FONT_8x16;
        case 2: return FONT_12x24;
        case 3: return FONT_16x32;
        default: return FONT_6x8;
    }
}

/* 获取字体宽度 (每字符像素数) */
static uint8_t gui_font_width(uint8_t font_id) {
    switch (font_id) {
        case 0: return 6;
        case 1: return 8;
        case 2: return 12;
        case 3: return 16;
        default: return 6;
    }
}

/* 获取字体高度 (像素) */
static uint8_t gui_font_height(uint8_t font_id) {
    switch (font_id) {
        case 0: return 8;
        case 1: return 16;
        case 2: return 24;
        case 3: return 32;
        default: return 8;
    }
}

/* ================================================================
 * 内部 —— 矩形辅助
 * ================================================================ */

/**
 * 判断两个矩形是否重叠
 */
static bool rect_overlap(const dirty_rect_t *a, const dirty_rect_t *b) {
    if (a->x >= b->x + b->w || b->x >= a->x + a->w) return false;
    if (a->y >= b->y + b->h || b->y >= a->y + a->h) return false;
    return true;
}

/**
 * 计算 a 和 b 的包围矩形 (写入 dst)
 */
static void rect_union(dirty_rect_t *dst, const dirty_rect_t *a, const dirty_rect_t *b) {
    uint16_t x1 = (a->x < b->x) ? a->x : b->x;
    uint16_t y1 = (a->y < b->y) ? a->y : b->y;
    uint16_t x2 = ((a->x + a->w) > (b->x + b->w)) ? (a->x + a->w) : (b->x + b->w);
    uint16_t y2 = ((a->y + a->h) > (b->y + b->h)) ? (a->y + a->h) : (b->y + b->h);
    dst->x = x1;
    dst->y = y1;
    dst->w = x2 - x1;
    dst->h = y2 - y1;
    dst->valid = true;
}

/* ================================================================
 * 公开 API —— 脏矩形追踪
 * ================================================================ */

/**
 * 标记一个屏幕区域为"脏" (需要刷新)
 *
 * 策略:
 *   1. 裁剪到屏幕边界 (0,0,LCD_WIDTH,LCD_HEIGHT)
 *   2. 尝试与现有脏矩形合并 (取并集)
 *   3. 若无重叠, 寻找空槽位写入
 *   4. 若槽位已满 (MAX_DIRTY_RECTS=8), 折叠为全屏矩形
 */
void gui_dirty_mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    /* 裁剪到屏幕范围 */
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (w == 0 || h == 0) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH  - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w == 0 || h == 0) return;

    dirty_rect_t new_rect;
    new_rect.x     = x;
    new_rect.y     = y;
    new_rect.w     = w;
    new_rect.h     = h;
    new_rect.valid = true;

    /* 尝试合并到已有的重叠矩形 */
    for (uint8_t i = 0; i < MAX_DIRTY_RECTS; i++) {
        if (!dirty_rects[i].valid) continue;
        if (!rect_overlap(&new_rect, &dirty_rects[i])) continue;

        /* 重叠 → 合并并放入当前槽位 */
        rect_union(&dirty_rects[i], &new_rect, &dirty_rects[i]);

        /* 合并后的矩形可能又与其它矩形重叠, 继续尝试 */
        new_rect = dirty_rects[i];
        for (uint8_t j = i + 1; j < MAX_DIRTY_RECTS; j++) {
            if (!dirty_rects[j].valid) continue;
            if (rect_overlap(&new_rect, &dirty_rects[j])) {
                rect_union(&dirty_rects[i], &new_rect, &dirty_rects[j]);
                dirty_rects[j].valid = false;   /* 清除被吸收的矩形 */
                new_rect = dirty_rects[i];
            }
        }
        return;
    }

    /* 无重叠 → 寻找空槽位 */
    for (uint8_t i = 0; i < MAX_DIRTY_RECTS; i++) {
        if (!dirty_rects[i].valid) {
            dirty_rects[i] = new_rect;
            return;
        }
    }

    /* 槽位已满 → 折叠为全屏矩形 */
    LOG("gui: dirty rects full, collapsing to full screen\r\n");
    dirty_rects[0].x     = 0;
    dirty_rects[0].y     = 0;
    dirty_rects[0].w     = LCD_WIDTH;
    dirty_rects[0].h     = LCD_HEIGHT;
    dirty_rects[0].valid = true;
    for (uint8_t i = 1; i < MAX_DIRTY_RECTS; i++) {
        dirty_rects[i].valid = false;
    }
}

/**
 * 清除所有脏矩形 (通常在刷新完成后调用)
 */
void gui_dirty_clear(void) {
    for (uint8_t i = 0; i < MAX_DIRTY_RECTS; i++) {
        dirty_rects[i].valid = false;
    }
}

/**
 * 返回当前有效脏矩形数量
 */
uint8_t gui_dirty_get_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_DIRTY_RECTS; i++) {
        if (dirty_rects[i].valid) count++;
    }
    return count;
}

/**
 * 返回脏矩形数组 (只读)
 */
const dirty_rect_t* gui_dirty_get_all(void) {
    return dirty_rects;
}

/**
 * 合并所有脏矩形为一个最小包围矩形
 *
 * 返回:
 *   若存在有效脏矩形 → 返回包围所有脏矩形的矩形 (valid=true)
 *   若无有效脏矩形 → 返回 zero-filled (valid=false)
 */
dirty_rect_t gui_dirty_merge(void) {
    dirty_rect_t merged;
    memset(&merged, 0, sizeof(merged));

    bool first = true;

    for (uint8_t i = 0; i < MAX_DIRTY_RECTS; i++) {
        if (!dirty_rects[i].valid) continue;

        if (first) {
            merged = dirty_rects[i];
            first  = false;
        } else {
            rect_union(&merged, &merged, &dirty_rects[i]);
        }
    }

    return merged;
}

/* ================================================================
 * 公开 API —— 初始化
 * ================================================================ */

void gui_init(void) {
    LOG("GUI init\r\n");
    gui_dirty_clear();
}

/* ================================================================
 * 公开 API —— 高级图形
 * ================================================================ */

/**
 * 绘制粗圆弧 (从 start_deg 到 end_deg, 顺时针)
 *
 * 0° = 3 点钟方向, 顺时针旋转 (与数学极坐标一致, y 轴向下)
 *
 * 实现: 逐层绘制 (t = 0 .. thickness-1), 每一层是在半径 (r+t) 处
 * 的 1 像素宽圆弧。角度步进为 1°, 适合 128×160 小屏幕。
 *
 * @param cx, cy       圆心坐标
 * @param r            内径 (像素)
 * @param start_deg    起始角度 (度, 0~359)
 * @param end_deg      终止角度 (度, 0~359)
 * @param thickness    圆弧粗细 (像素)
 * @param color        颜色 (RGB565)
 */
void gui_draw_arc(uint16_t cx, uint16_t cy, uint16_t r,
                  int16_t start_deg, int16_t end_deg,
                  uint16_t thickness, uint16_t color) {
    if (thickness == 0 || r == 0) return;

    /* 归一化角度到 [0, 360) */
    start_deg = start_deg % 360;
    if (start_deg < 0) start_deg += 360;
    end_deg = end_deg % 360;
    if (end_deg < 0) end_deg += 360;

    /* 计算角度范围 (处理跨越 0° 的情况) */
    int16_t range;
    if (end_deg >= start_deg) {
        range = end_deg - start_deg;
    } else {
        range = 360 - start_deg + end_deg;
    }
    if (range == 0 && start_deg == end_deg) return;   /* 空弧 */

    /* 逐层绘制 */
    for (uint16_t t = 0; t < thickness; t++) {
        uint16_t R = r + t;
        if (R == 0) continue;

        for (int16_t d = 0; d <= range; d++) {
            int16_t deg = start_deg + d;
            if (deg >= 360) deg -= 360;

            float rad = (float)deg * M_PI / 180.0f;
            int16_t px = (int16_t)(cx + (int16_t)((float)R * cosf(rad) + 0.5f));
            int16_t py = (int16_t)(cy + (int16_t)((float)R * sinf(rad) + 0.5f));

            st7735_draw_pixel((uint16_t)px, (uint16_t)py, color);
        }
    }

    /* 标记脏矩形 (保守: 整个圆的外接矩形) */
    {
        uint16_t outer_r = r + thickness;
        uint16_t bx = (cx > outer_r) ? (uint16_t)(cx - outer_r) : 0;
        uint16_t by = (cy > outer_r) ? (uint16_t)(cy - outer_r) : 0;
        uint16_t bw = outer_r * 2;
        uint16_t bh = outer_r * 2;
        if (bx + bw > LCD_WIDTH)  bw = LCD_WIDTH  - bx;
        if (by + bh > LCD_HEIGHT) bh = LCD_HEIGHT - by;
        gui_dirty_mark(bx, by, bw, bh);
    }
}

/**
 * 绘制弧形刻度线
 *
 * 在从 start_deg 到 end_deg 的圆弧上均匀分布 count 条短线段。
 * 每条刻度线长度 7 像素, 从半径 r-5 延伸到 r-12 (向圆心方向)。
 *
 * @param cx, cy       圆心坐标
 * @param r            圆弧半径
 * @param start_deg    起始角度 (度)
 * @param end_deg      终止角度 (度)
 * @param count        刻度线数量
 * @param color        颜色 (RGB565)
 */
void gui_draw_tick_marks(uint16_t cx, uint16_t cy, uint16_t r,
                         int16_t start_deg, int16_t end_deg,
                         uint8_t count, uint16_t color) {
    if (count == 0 || r < 13) return;  /* r 不够大则无法容纳 7px 刻度 */

    /* 归一化角度 */
    start_deg = start_deg % 360;
    if (start_deg < 0) start_deg += 360;
    end_deg = end_deg % 360;
    if (end_deg < 0) end_deg += 360;

    /* 计算总角度范围 */
    float total_range;
    if (end_deg >= start_deg) {
        total_range = (float)(end_deg - start_deg);
    } else {
        total_range = (float)(360 - start_deg + end_deg);
    }

    /* 角度步进 */
    float step = (count > 1) ? total_range / (float)(count - 1) : 0.0f;

    for (uint8_t i = 0; i < count; i++) {
        float angle = (float)start_deg + step * (float)i;
        if (angle >= 360.0f) angle -= 360.0f;

        float rad = angle * M_PI / 180.0f;
        float c   = cosf(rad);
        float s   = sinf(rad);

        /* 刻度从 r-5 (外) 到 r-12 (内), 共 7 像素 */
        int16_t x1 = (int16_t)(cx + (int16_t)((float)(r - 5)  * c + 0.5f));
        int16_t y1 = (int16_t)(cy + (int16_t)((float)(r - 5)  * s + 0.5f));
        int16_t x2 = (int16_t)(cx + (int16_t)((float)(r - 12) * c + 0.5f));
        int16_t y2 = (int16_t)(cy + (int16_t)((float)(r - 12) * s + 0.5f));

        st7735_draw_line((uint16_t)x1, (uint16_t)y1,
                         (uint16_t)x2, (uint16_t)y2, color);
    }

    /* 标记脏矩形 */
    {
        uint16_t bx = (cx > r) ? (uint16_t)(cx - r) : 0;
        uint16_t by = (cy > r) ? (uint16_t)(cy - r) : 0;
        uint16_t bw = r * 2;
        uint16_t bh = r * 2;
        if (bx + bw > LCD_WIDTH)  bw = LCD_WIDTH  - bx;
        if (by + bh > LCD_HEIGHT) bh = LCD_HEIGHT - by;
        gui_dirty_mark(bx, by, bw, bh);
    }
}

/**
 * 绘制数字仪表 (大号居中数值 + 小号单位)
 *
 * 布局:
 *   ┌──────────────────────┐
 *   │                      │
 *   │       value          │  ← FONT_12x24, color_val
 *   │       unit           │  ← FONT_6x8,  color_unit
 *   │                      │
 *   └──────────────────────┘
 *
 * 包围盒 = max(value_w, unit_w) × (value_h + unit_h + 4)
 * 背景清为 COLOR_BLACK。
 *
 * @param cx, cy       包围盒中心坐标
 * @param value        数值字符串 (如 "36.5")
 * @param unit         单位字符串 (如 "°C")
 * @param color_val    数值颜色
 * @param color_unit   单位颜色
 */
void gui_draw_meter(uint16_t cx, uint16_t cy,
                    const char *value, const char *unit,
                    uint16_t color_val, uint16_t color_unit) {
    if (!value) value = "";
    if (!unit)  unit  = "";

    /* 计算文本尺寸 */
    uint16_t value_w, value_h, unit_w, unit_h;
    gui_text_size(value, 2, &value_w, &value_h);   /* font_id=2 → FONT_12x24 */
    gui_text_size(unit,  0, &unit_w,  &unit_h);    /* font_id=0 → FONT_6x8   */

    uint16_t box_w = (value_w > unit_w) ? value_w : unit_w;
    uint16_t box_h = value_h + unit_h + 4;          /* 4 px 间距 */

    /* 包围盒左上角 */
    int16_t box_x = (int16_t)cx - (int16_t)(box_w / 2);
    int16_t box_y = (int16_t)cy - (int16_t)(box_h / 2);
    if (box_x < 0) box_x = 0;
    if (box_y < 0) box_y = 0;

    /* 清除背景 */
    st7735_fill_rect((uint16_t)box_x, (uint16_t)box_y,
                     box_w, box_h, COLOR_BLACK);

    /* 数值 (居中, 顶部) */
    {
        int16_t vx = (int16_t)cx - (int16_t)(value_w / 2);
        int16_t vy = box_y;
        if (vx < 0) vx = 0;
        st7735_draw_text((uint16_t)vx, (uint16_t)vy, value,
                         FONT_12x24, color_val, COLOR_BLACK);
    }

    /* 单位 (居中, 数值下方 4px 间距) */
    {
        int16_t ux = (int16_t)cx - (int16_t)(unit_w / 2);
        int16_t uy = box_y + (int16_t)value_h + 4;
        if (ux < 0) ux = 0;
        st7735_draw_text((uint16_t)ux, (uint16_t)uy, unit,
                         FONT_6x8, color_unit, COLOR_BLACK);
    }

    /* 标记脏矩形 */
    gui_dirty_mark((uint16_t)box_x, (uint16_t)box_y, box_w, box_h);
}

/* ================================================================
 * 图标位图 (8×8 单色)
 *
 * 每个字节对应一行, bit7 (0x80) 为最左像素, bit0 (0x01) 为最右像素。
 * ================================================================ */
static const uint8_t icon_bitmaps[][8] = {
    /* ICON_SUN */
    {0x24, 0x18, 0x7E, 0x7E, 0x7E, 0x7E, 0x18, 0x24},
    /* ICON_MOON */
    {0x3C, 0x78, 0x70, 0x60, 0x60, 0x70, 0x78, 0x3C},
    /* ICON_ARROW_UP */
    {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* ICON_ARROW_DOWN */
    {0x00, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18},
    /* ICON_ARROW_LEFT */
    {0x00, 0x08, 0x18, 0x3E, 0x3E, 0x18, 0x08, 0x00},
    /* ICON_ARROW_RIGHT */
    {0x00, 0x10, 0x18, 0x7C, 0x7C, 0x18, 0x10, 0x00},
    /* ICON_CHECK */
    {0x01, 0x03, 0x06, 0x0C, 0x58, 0x70, 0x20, 0x00},
    /* ICON_CROSS */
    {0x41, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x41},
};

/**
 * 绘制预设图标 (8×8 单色位图, 像素级逐点绘制)
 *
 * @param x, y      左上角坐标
 * @param icon      图标类型 (gui_icon_t 枚举)
 * @param color     前景色 (RGB565, 背景透明)
 */
void gui_draw_icon(uint16_t x, uint16_t y, gui_icon_t icon, uint16_t color) {
    if ((uint8_t)icon > (uint8_t)ICON_CROSS) return;

    const uint8_t *rows = icon_bitmaps[(uint8_t)icon];

    for (uint8_t row = 0; row < 8; row++) {
        uint8_t bits = rows[row];
        for (uint8_t col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                st7735_draw_pixel((uint16_t)(x + col),
                                  (uint16_t)(y + row), color);
            }
        }
    }

    /* 标记脏矩形 */
    gui_dirty_mark(x, y, 8, 8);
}

/* ================================================================
 * 公开 API —— 文本布局辅助
 * ================================================================ */

/**
 * 获取字符串渲染尺寸
 *
 * @param str       字符串 (支持 ASCII 0x20-0x7E)
 * @param font_id   字体 ID (0=FONT_6x8, 1=FONT_8x16, 2=FONT_12x24, 3=FONT_16x32)
 * @param w         输出: 像素宽度
 * @param h         输出: 像素高度
 */
void gui_text_size(const char *str, uint8_t font_id, uint16_t *w, uint16_t *h) {
    if (!str) str = "";
    if (!w || !h) return;

    uint8_t cw = gui_font_width(font_id);
    uint8_t ch = gui_font_height(font_id);

    *w = (uint16_t)(cw * (uint8_t)strlen(str));
    *h = ch;
}

/**
 * 居中绘制文本
 *
 * 文本包围盒中心位于 (cx, cy)。
 * 实际绘制坐标 = (cx - w/2, cy - h/2)。
 *
 * @param cx, cy    文本中心点
 * @param str       字符串
 * @param font_id   字体 ID
 * @param fg        前景色 (RGB565)
 * @param bg        背景色 (RGB565)
 */
void gui_draw_text_centered(uint16_t cx, uint16_t cy,
                            const char *str, uint8_t font_id,
                            uint16_t fg, uint16_t bg) {
    if (!str) str = "";

    uint16_t tw, th;
    gui_text_size(str, font_id, &tw, &th);

    /* 左上角 = 中心 - 半宽/半高 */
    int16_t x = (int16_t)cx - (int16_t)(tw / 2);
    int16_t y = (int16_t)cy - (int16_t)(th / 2);
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    font_size_t font = gui_map_font(font_id);

    st7735_draw_text((uint16_t)x, (uint16_t)y, str, font, fg, bg);

    /* 标记脏矩形 */
    {
        uint16_t dx = (uint16_t)x;
        uint16_t dy = (uint16_t)y;
        if (dx + tw > LCD_WIDTH)  tw = LCD_WIDTH  - dx;
        if (dy + th > LCD_HEIGHT) th = LCD_HEIGHT - dy;
        gui_dirty_mark(dx, dy, tw, th);
    }
}

/**
 * 按对齐方式绘制文本
 *
 * @param y         Y 坐标 (文本行顶部)
 * @param str       字符串
 * @param font_id   字体 ID
 * @param fg        前景色 (RGB565)
 * @param bg        背景色 (RGB565)
 * @param align     对齐方式:
 *                    GUI_ALIGN_LEFT   → x = 2
 *                    GUI_ALIGN_CENTER → x = (LCD_WIDTH - w) / 2
 *                    GUI_ALIGN_RIGHT  → x = LCD_WIDTH - w - 2
 */
void gui_draw_text_aligned(uint16_t y, const char *str, uint8_t font_id,
                           uint16_t fg, uint16_t bg, gui_align_t align) {
    if (!str) str = "";

    uint16_t tw, th;
    gui_text_size(str, font_id, &tw, &th);

    uint16_t x;
    switch (align) {
        case GUI_ALIGN_LEFT:
            x = 2;
            break;
        case GUI_ALIGN_RIGHT:
            x = (tw < LCD_WIDTH - 2) ? (LCD_WIDTH - tw - 2) : 0;
            break;
        case GUI_ALIGN_CENTER:
        default:
            x = (tw < LCD_WIDTH) ? ((LCD_WIDTH - tw) / 2) : 0;
            break;
    }

    font_size_t font = gui_map_font(font_id);

    st7735_draw_text(x, y, str, font, fg, bg);

    /* 标记脏矩形 */
    {
        uint16_t dx = x;
        uint16_t dy = y;
        if (dx + tw > LCD_WIDTH)  tw = LCD_WIDTH  - dx;
        if (dy + th > LCD_HEIGHT) th = LCD_HEIGHT - dy;
        gui_dirty_mark(dx, dy, tw, th);
    }
}

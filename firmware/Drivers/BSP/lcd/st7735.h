/**
 * st7735.h — ST7735S TFT LCD 驱动接口 (SPI)
 */

#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 颜色定义
 * ================================================================ */
typedef uint16_t color_t;

#define COLOR_WHITE         0xFFFF
#define COLOR_BLACK         0x0000
#define COLOR_RED           0xF800
#define COLOR_GREEN         0x07E0
#define COLOR_BLUE          0x001F
#define COLOR_YELLOW        0xFFE0
#define COLOR_CYAN          0x07FF
#define COLOR_MAGENTA       0xF81F
#define COLOR_GRAY          0x8410
#define COLOR_DARK_GRAY     0x4208
#define COLOR_ORANGE        0xFD20

#define RGB565(r, g, b)     ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/* ================================================================
 * 初始化 & 控制
 * ================================================================ */
void st7735_init(void);
void st7735_reset(void);
void st7735_sleep(bool enable);
void st7735_set_brightness(uint8_t pct);    /* 0-100 */
void st7735_set_rotation(uint8_t rotation); /* 0/1/2/3 */

/* ================================================================
 * 基本绘图
 * ================================================================ */
void st7735_fill_screen(color_t color);
void st7735_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color);
void st7735_draw_pixel(uint16_t x, uint16_t y, color_t color);
void st7735_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, color_t color);
void st7735_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color);
void st7735_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color);
void st7735_fill_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color);

/* ================================================================
 * 字符 & 文本
 * ================================================================ */
typedef enum {
    FONT_6x8,
    FONT_8x16,
    FONT_12x24,
    FONT_16x32,
} font_size_t;

void st7735_draw_char(uint16_t x, uint16_t y, char c, font_size_t font, color_t fg, color_t bg);
void st7735_draw_text(uint16_t x, uint16_t y, const char *str, font_size_t font, color_t fg, color_t bg);

/* ================================================================
 * 位图 (从 Flash 加载)
 * ================================================================ */
void st7735_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data);

/* ================================================================
 * 区域设置 (用于局部刷新)
 * ================================================================ */
void st7735_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void st7735_write_pixels(const uint8_t *data, uint32_t len);

/* ================================================================
 * 帧缓冲刷新
 * ================================================================ */
void st7735_flush(void);                                          /* 全屏刷新 (模式切换时使用) */
void st7735_flush_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h); /* 局部刷新 (日常使用, 避免闪烁) */

/* ================================================================
 * 背光控制
 * ================================================================ */
void st7735_set_backlight_pwm(uint16_t duty); /* 0-1000 */

#endif /* ST7735_H */

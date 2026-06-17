/**
 * st7735.c — ST7735S TFT LCD 驱动实现 (128×160, SPI, RGB565, framebuffer)
 *
 * 架构: 全帧缓冲 (128×160×2 = 40KB) + 脏矩形局部刷新
 *   所有绘图操作写入 RAM 帧缓冲, 不触发 SPI 传输。
 *   调用 st7735_flush_rect() 仅发送变化区域到 TFT, 无闪烁。
 *
 * 依赖:
 *   - main.h        → HAL 外设句柄 (hspi1, htim3)
 *   - pin_config.h  → 引脚宏 (LCD_CS, LCD_DC, LCD_RST, LCD_BL 等)
 *   - app_config.h  → LOG 宏, LCD_BRIGHTNESS_DEFAULT
 */

#include "st7735.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"
#include <string.h>

/* ---- HAL 句柄 (在 main.c 中定义) ---- */
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim3;

/* ================================================================
 * 帧缓冲 — 40KB 静态分配 (STM32F407 192KB SRAM 够用)
 * ================================================================ */
static color_t g_fb[LCD_WIDTH * LCD_HEIGHT];

/* ================================================================
 * 内部常量 —— ST7735S 初始化序列
 * ================================================================ */
static const uint8_t init_sequence[] = {
    /* 软件复位 */
    0x01, 0x00,                         // SWRESET
    0xFF, 150,                          // 延时 150ms

    /* 退出休眠 */
    0x11, 0x00,                         // SLPOUT
    0xFF, 150,                          // 延时 150ms

    /* ---- 帧速率控制 ---- */
    0xB1, 0x03, 0x01, 0x2C, 0x2D,       // FRMCTR1
    0xB2, 0x03, 0x01, 0x2C, 0x2D,       // FRMCTR2
    0xB3, 0x06,
    0x01, 0x2C, 0x2D,                   // FRMCTR3
    0x01, 0x2C, 0x2D,

    /* 显示反转 */
    0xB4, 0x01, 0x07,                   // INVCTR: dot inversion

    /* ---- 电源设置 ---- */
    0xC0, 0x03, 0xA2, 0x02, 0x84,       // PWCTR1: GVDD=4.7V
    0xC1, 0x01, 0xC5,                   // PWCTR2: VGH/VGL
    0xC2, 0x02, 0x0A, 0x00,             // PWCTR3: op-amp current
    0xC3, 0x02, 0x8A, 0x2A,             // PWCTR4: op-amp current
    0xC4, 0x02, 0x8A, 0xEE,             // PWCTR5

    /* VCOM 控制 */
    0xC5, 0x01, 0x0E,                   // VMCTR1

    /* ---- Gamma 校正 ---- */
    0xE0, 0x10,
    0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22,
    0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10,

    0xE1, 0x10,
    0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E,
    0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10,

    /* ---- 像素格式 ---- */
    0x3A, 0x01, 0x05,                   // COLMOD: 16-bit/pixel (RGB565)

    /* ---- 显示方向 ---- */
    0x36, 0x01, 0xC8,                   // MADCTL: MY=1, MX=1, MV=1

    /* 正常显示模式 */
    0x13, 0x00,                         // NORON

    /* 开启显示 */
    0x29, 0x00,                         // DISPON
    0xFF, 100,                          // 延时 100ms

    /* 普通模式 (非反转) */
    0x20, 0x00,                         // INVOFF

    0x00  /* 结束标记 */
};

/* ================================================================
 * 内部辅助 —— CS / DC 引脚操作
 * ================================================================ */
static inline void cs_low(void)  { HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET);   }
static inline void dc_low(void)  { HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_RESET); }
static inline void dc_high(void) { HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_SET);   }

static void lcd_write_cmd(uint8_t cmd) {
    cs_low();
    dc_low();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    cs_high();
}

static void lcd_write_data8(uint8_t data) {
    cs_low();
    dc_high();
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
    cs_high();
}

static bool clip_rect(uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h) {
    if (*x >= LCD_WIDTH  || *y >= LCD_HEIGHT) return false;
    if (*w == 0 || *h == 0) return false;
    if (*x + *w > LCD_WIDTH)  *w = LCD_WIDTH  - *x;
    if (*y + *h > LCD_HEIGHT) *h = LCD_HEIGHT - *y;
    return (*w > 0 && *h > 0);
}

/* ================================================================
 * 帧缓冲像素写入
 * ================================================================ */
static inline void fb_set(uint16_t x, uint16_t y, color_t c) {
    if (x < LCD_WIDTH && y < LCD_HEIGHT) {
        g_fb[(uint32_t)y * LCD_WIDTH + x] = c;
    }
}

/* ================================================================
 * 公开 API —— 初始化 & 控制
 * ================================================================ */

void st7735_init(void) {
    LOG("ST7735S init start\r\n");

    st7735_reset();

    const uint8_t *p = init_sequence;
    while (1) {
        uint8_t cmd = *p++;
        if (cmd == 0x00) break;

        uint8_t arg_len = *p++;

        if (cmd == 0xFF) {
            HAL_Delay(arg_len);
        } else {
            lcd_write_cmd(cmd);
            for (uint8_t i = 0; i < arg_len; i++) {
                lcd_write_data8(*p++);
            }
        }
    }

    /* 填充帧缓冲为黑色 */
    memset(g_fb, 0, sizeof(g_fb));

    /* 首次刷新: 将全黑帧缓冲发送到TFT GRAM, 避免显示上电随机值 */
    st7735_flush();

    st7735_set_brightness(LCD_BRIGHTNESS_DEFAULT);
    st7735_set_rotation(LCD_ROTATION);

    LOG("ST7735S init done\r\n");
}

void st7735_reset(void) {
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(20);
}

void st7735_sleep(bool enable) {
    if (enable) {
        lcd_write_cmd(0x10);
    } else {
        lcd_write_cmd(0x11);
        HAL_Delay(120);
    }
}

void st7735_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    st7735_set_backlight_pwm((uint16_t)pct * 10);
}

void st7735_set_rotation(uint8_t rotation) {
    uint8_t madctl;
    switch (rotation & 0x03) {
        case 0:  madctl = 0xC8; break;
        case 1:  madctl = 0x68; break;
        case 2:  madctl = 0x08; break;
        case 3:  madctl = 0xA8; break;
        default: madctl = 0xC8; break;
    }
    lcd_write_cmd(0x36);
    lcd_write_data8(madctl);
}

/* ================================================================
 * 帧缓冲刷新 —— 唯一触发 SPI 传输的地方
 *
 * 策略: 局部刷新替代全屏刷新，解决闪烁问题
 *
 *   旧方案: st7735_flush() → DMA 全屏 40KB → 约 30ms 逐行刷出 → 肉眼可见闪烁
 *   新方案: st7735_flush_rect() → 仅发送变化区域的像素 → 典型 2-5KB → <5ms
 *
 *   字节序: g_fb 存储为 native uint16_t (little-endian ARM)。
 *          ST7735 期望 big-endian (hi-byte first)。
 *          flush_rect 逐行字节交换后发送，保证颜色正确。
 * ================================================================ */

/**
 * @brief 局部刷新 — 将帧缓冲中的矩形区域发送到 TFT
 *
 * 仅传输变化区域 (典型: 时钟秒数更新 ~96×24=2.3KB, 约 3.5ms)
 * 逐行从 g_fb 读取、字节交换为 big-endian、SPI 发送。
 *
 * @param x, y  矩形左上角 (像素坐标)
 * @param w, h  矩形宽高 (像素)
 */
void st7735_flush_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!clip_rect(&x, &y, &w, &h)) return;

    uint16_t x2 = x + w - 1;
    uint16_t y2 = y + h - 1;

    /* CASET: 列地址窗口 */
    lcd_write_cmd(0x2A);
    lcd_write_data8(x >> 8);
    lcd_write_data8(x & 0xFF);
    lcd_write_data8(x2 >> 8);
    lcd_write_data8(x2 & 0xFF);

    /* RASET: 行地址窗口 */
    lcd_write_cmd(0x2B);
    lcd_write_data8(y >> 8);
    lcd_write_data8(y & 0xFF);
    lcd_write_data8(y2 >> 8);
    lcd_write_data8(y2 & 0xFF);

    /* RAMWR: 内存写入 */
    lcd_write_cmd(0x2C);

    cs_low();
    dc_high();

    /*
     * 逐行发送: g_fb 是 little-endian uint16_t (lo, hi in memory)
     * ST7735 需要 big-endian (hi, lo on wire)
     * → 逐行字节交换到 row_buf 后 SPI 发送
     */
    uint8_t row_buf[LCD_WIDTH * 2];   /* 最大 256 字节, 栈上分配 */

    for (uint16_t row = y; row <= y2; row++) {
        color_t *src = &g_fb[(uint32_t)row * LCD_WIDTH + x];
        uint8_t *dst = row_buf;
        for (uint16_t col = 0; col < w; col++) {
            *dst++ = (uint8_t)(*src >> 8);    /* hi byte first (big-endian wire) */
            *dst++ = (uint8_t)(*src);          /* lo byte */
            src++;
        }
        HAL_SPI_Transmit(&hspi1, row_buf, (uint32_t)w * 2, HAL_MAX_DELAY);
    }

    cs_high();
}

/**
 * @brief 全屏刷新 — 将整个帧缓冲发送到 TFT
 *
 * 仅在模式切换 (全屏重绘) 时使用。
 * 日常更新请用 st7735_flush_rect() 避免闪烁。
 */
void st7735_flush(void) {
    st7735_flush_rect(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void st7735_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    // no-op: framebuffer 模式下不需要设窗
    (void)x; (void)y; (void)w; (void)h;
}

void st7735_write_pixels(const uint8_t *data, uint32_t len) {
    // no-op: framebuffer 模式下不需要逐像素发送
    (void)data; (void)len;
}

/* ================================================================
 * 基本绘图 —— 全部写入帧缓冲
 * ================================================================ */

void st7735_fill_screen(color_t color) {
    /* 直接整片填充 g_fb */
    uint16_t *p = g_fb;
    for (uint32_t i = 0; i < (uint32_t)(LCD_WIDTH * LCD_HEIGHT); i++) {
        *p++ = color;
    }
}

void st7735_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color) {
    if (!clip_rect(&x, &y, &w, &h)) return;
    for (uint16_t row = y; row < y + h; row++) {
        color_t *p = &g_fb[row * LCD_WIDTH + x];
        for (uint16_t col = 0; col < w; col++) {
            *p++ = color;
        }
    }
}

void st7735_draw_pixel(uint16_t x, uint16_t y, color_t color) {
    fb_set(x, y, color);
}

void st7735_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, color_t color) {
    int16_t dx  = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t dy  = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    int16_t sx  = (x0 < x1) ? 1 : -1;
    int16_t sy  = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    int16_t e2;

    while (1) {
        fb_set(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 = (uint16_t)((int16_t)x0 + sx); }
        if (e2 <  dx) { err += dx; y0 = (uint16_t)((int16_t)y0 + sy); }
    }
}

void st7735_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color) {
    if (w == 0 || h == 0) return;
    uint16_t x2 = x + w - 1;
    uint16_t y2 = y + h - 1;
    st7735_fill_rect(x, y, w, 1, color);
    st7735_fill_rect(x, y2, w, 1, color);
    if (h > 2) st7735_fill_rect(x, y + 1, 1, h - 2, color);
    if (h > 2) st7735_fill_rect(x2, y + 1, 1, h - 2, color);
}

void st7735_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color) {
    if (r == 0) { fb_set(cx, cy, color); return; }

    int16_t f     = 1 - (int16_t)r;
    int16_t ddf_x = 1;
    int16_t ddf_y = -2 * (int16_t)r;
    int16_t x     = 0;
    int16_t y     = (int16_t)r;

    fb_set(cx, (uint16_t)(cy + r), color);
    fb_set(cx, (uint16_t)(cy - r), color);
    fb_set((uint16_t)(cx + r), cy, color);
    fb_set((uint16_t)(cx - r), cy, color);

    while (x < y) {
        if (f >= 0) { y--; ddf_y += 2; f += ddf_y; }
        x++;
        ddf_x += 2;
        f += ddf_x;

        fb_set((uint16_t)(cx + x), (uint16_t)(cy + y), color);
        fb_set((uint16_t)(cx - x), (uint16_t)(cy + y), color);
        fb_set((uint16_t)(cx + x), (uint16_t)(cy - y), color);
        fb_set((uint16_t)(cx - x), (uint16_t)(cy - y), color);
        fb_set((uint16_t)(cx + y), (uint16_t)(cy + x), color);
        fb_set((uint16_t)(cx - y), (uint16_t)(cy + x), color);
        fb_set((uint16_t)(cx + y), (uint16_t)(cy - x), color);
        fb_set((uint16_t)(cx - y), (uint16_t)(cy - x), color);
    }
}

void st7735_fill_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color) {
    int16_t f     = 1 - (int16_t)r;
    int16_t ddf_x = 1;
    int16_t ddf_y = -2 * (int16_t)r;
    int16_t x     = 0;
    int16_t y     = (int16_t)r;

    while (x < y) {
        if (f >= 0) { y--; ddf_y += 2; f += ddf_y; }
        x++;
        ddf_x += 2;
        f += ddf_x;

        for (int16_t i = (int16_t)cx - x; i <= (int16_t)cx + x; i++) {
            fb_set((uint16_t)i, (uint16_t)(cy + y), color);
            fb_set((uint16_t)i, (uint16_t)(cy - y), color);
        }
        for (int16_t i = (int16_t)cx - y; i <= (int16_t)cx + y; i++) {
            fb_set((uint16_t)i, (uint16_t)(cy + x), color);
            fb_set((uint16_t)i, (uint16_t)(cy - x), color);
        }
    }
}

/* ================================================================
 * 5×7 点阵 ASCII 字库
 * ================================================================ */
static const uint8_t font_5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /*  32 SPACE */
    {0x00,0x00,0x5F,0x00,0x00}, /*  33 !     */
    {0x00,0x07,0x00,0x07,0x00}, /*  34 "     */
    {0x14,0x7F,0x14,0x7F,0x14}, /*  35 #     */
    {0x24,0x2A,0x7F,0x2A,0x12}, /*  36 $     */
    {0x23,0x13,0x08,0x64,0x62}, /*  37 %     */
    {0x36,0x49,0x55,0x22,0x50}, /*  38 &     */
    {0x00,0x05,0x03,0x00,0x00}, /*  39 '     */
    {0x00,0x1C,0x22,0x41,0x00}, /*  40 (     */
    {0x00,0x41,0x22,0x1C,0x00}, /*  41 )     */
    {0x14,0x08,0x3E,0x08,0x14}, /*  42 *     */
    {0x08,0x08,0x3E,0x08,0x08}, /*  43 +     */
    {0x00,0x50,0x30,0x00,0x00}, /*  44 ,     */
    {0x08,0x08,0x08,0x08,0x08}, /*  45 -     */
    {0x00,0x60,0x60,0x00,0x00}, /*  46 .     */
    {0x20,0x10,0x08,0x04,0x02}, /*  47 /     */
    {0x3E,0x51,0x49,0x45,0x3E}, /*  48 0     */
    {0x00,0x42,0x7F,0x40,0x00}, /*  49 1     */
    {0x42,0x61,0x51,0x49,0x46}, /*  50 2     */
    {0x21,0x41,0x45,0x4B,0x31}, /*  51 3     */
    {0x18,0x14,0x12,0x7F,0x10}, /*  52 4     */
    {0x27,0x45,0x45,0x45,0x39}, /*  53 5     */
    {0x3C,0x4A,0x49,0x49,0x30}, /*  54 6     */
    {0x01,0x71,0x09,0x05,0x03}, /*  55 7     */
    {0x36,0x49,0x49,0x49,0x36}, /*  56 8     */
    {0x06,0x49,0x49,0x29,0x1E}, /*  57 9     */
    {0x00,0x36,0x36,0x00,0x00}, /*  58 :     */
    {0x00,0x56,0x36,0x00,0x00}, /*  59 ;     */
    {0x08,0x14,0x22,0x41,0x00}, /*  60 <     */
    {0x14,0x14,0x14,0x14,0x14}, /*  61 =     */
    {0x00,0x41,0x22,0x14,0x08}, /*  62 >     */
    {0x02,0x01,0x51,0x09,0x06}, /*  63 ?     */
    {0x32,0x49,0x79,0x41,0x3E}, /*  64 @     */
    {0x7E,0x11,0x11,0x11,0x7E}, /*  65 A     */
    {0x7F,0x49,0x49,0x49,0x36}, /*  66 B     */
    {0x3E,0x41,0x41,0x41,0x22}, /*  67 C     */
    {0x7F,0x41,0x41,0x22,0x1C}, /*  68 D     */
    {0x7F,0x49,0x49,0x49,0x41}, /*  69 E     */
    {0x7F,0x09,0x09,0x09,0x01}, /*  70 F     */
    {0x3E,0x41,0x49,0x49,0x7A}, /*  71 G     */
    {0x7F,0x08,0x08,0x08,0x7F}, /*  72 H     */
    {0x00,0x41,0x7F,0x41,0x00}, /*  73 I     */
    {0x20,0x40,0x41,0x3F,0x01}, /*  74 J     */
    {0x7F,0x08,0x14,0x22,0x41}, /*  75 K     */
    {0x7F,0x40,0x40,0x40,0x40}, /*  76 L     */
    {0x7F,0x02,0x0C,0x02,0x7F}, /*  77 M     */
    {0x7F,0x04,0x08,0x10,0x7F}, /*  78 N     */
    {0x3E,0x41,0x41,0x41,0x3E}, /*  79 O     */
    {0x7F,0x09,0x09,0x09,0x06}, /*  80 P     */
    {0x3E,0x41,0x51,0x21,0x5E}, /*  81 Q     */
    {0x7F,0x09,0x19,0x29,0x46}, /*  82 R     */
    {0x26,0x49,0x49,0x49,0x32}, /*  83 S     */
    {0x01,0x01,0x7F,0x01,0x01}, /*  84 T     */
    {0x3F,0x40,0x40,0x40,0x3F}, /*  85 U     */
    {0x1F,0x20,0x40,0x20,0x1F}, /*  86 V     */
    {0x3F,0x40,0x30,0x40,0x3F}, /*  87 W     */
    {0x63,0x14,0x08,0x14,0x63}, /*  88 X     */
    {0x03,0x04,0x78,0x04,0x03}, /*  89 Y     */
    {0x61,0x51,0x49,0x45,0x43}, /*  90 Z     */
    {0x00,0x7F,0x41,0x41,0x00}, /*  91 [     */
    {0x02,0x04,0x08,0x10,0x20}, /*  92 \     */
    {0x00,0x41,0x41,0x7F,0x00}, /*  93 ]     */
    {0x04,0x02,0x01,0x02,0x04}, /*  94 ^     */
    {0x40,0x40,0x40,0x40,0x40}, /*  95 _     */
    {0x00,0x01,0x02,0x04,0x00}, /*  96 `     */
    {0x20,0x54,0x54,0x54,0x78}, /*  97 a     */
    {0x7F,0x48,0x44,0x44,0x38}, /*  98 b     */
    {0x38,0x44,0x44,0x44,0x20}, /*  99 c     */
    {0x38,0x44,0x44,0x48,0x7F}, /* 100 d     */
    {0x38,0x54,0x54,0x54,0x18}, /* 101 e     */
    {0x08,0x7E,0x09,0x01,0x02}, /* 102 f     */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 103 g     */
    {0x7F,0x08,0x04,0x04,0x78}, /* 104 h     */
    {0x00,0x44,0x7D,0x40,0x00}, /* 105 i     */
    {0x20,0x40,0x44,0x3D,0x00}, /* 106 j     */
    {0x7F,0x10,0x28,0x44,0x00}, /* 107 k     */
    {0x00,0x41,0x7F,0x40,0x00}, /* 108 l     */
    {0x7C,0x04,0x18,0x04,0x78}, /* 109 m     */
    {0x7C,0x08,0x04,0x04,0x78}, /* 110 n     */
    {0x38,0x44,0x44,0x44,0x38}, /* 111 o     */
    {0x7C,0x14,0x14,0x14,0x08}, /* 112 p     */
    {0x08,0x14,0x14,0x18,0x7C}, /* 113 q     */
    {0x7C,0x08,0x04,0x04,0x08}, /* 114 r     */
    {0x48,0x54,0x54,0x54,0x20}, /* 115 s     */
    {0x04,0x3F,0x44,0x40,0x20}, /* 116 t     */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 117 u     */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 118 v     */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 119 w     */
    {0x44,0x28,0x10,0x28,0x44}, /* 120 x     */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 121 y     */
    {0x44,0x64,0x54,0x4C,0x44}, /* 122 z     */
    {0x00,0x08,0x36,0x41,0x00}, /* 123 {     */
    {0x00,0x00,0x7F,0x00,0x00}, /* 124 |     */
    {0x00,0x41,0x36,0x08,0x00}, /* 125 }     */
    {0x08,0x04,0x08,0x10,0x08}, /* 126 ~     */
};

/* ================================================================
 * 字体尺寸
 * ================================================================ */
static void font_get_size(font_size_t font, uint8_t *cw, uint8_t *ch) {
    switch (font) {
        case FONT_6x8:   *cw = 6;  *ch = 8;  break;
        case FONT_8x16:  *cw = 8;  *ch = 16; break;
        case FONT_12x24: *cw = 12; *ch = 24; break;
        case FONT_16x32: *cw = 16; *ch = 32; break;
        default:         *cw = 6;  *ch = 8;  break;
    }
}

static uint8_t font_scale(font_size_t font) {
    switch (font) {
        case FONT_6x8:   return 1;
        case FONT_8x16:  return 1;
        case FONT_12x24: return 2;
        case FONT_16x32: return 3;
        default:         return 1;
    }
}

/* ================================================================
 * 字符 & 文本 — 写入帧缓冲 (零 SPI 开销)
 * ================================================================ */

void st7735_draw_char(uint16_t x, uint16_t y, char c, font_size_t font, color_t fg, color_t bg) {
    if (c < 0x20 || c > 0x7E) c = ' ';
    uint8_t idx = (uint8_t)(c - 0x20);

    uint8_t cw, ch;
    font_get_size(font, &cw, &ch);
    uint8_t scale = font_scale(font);

    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    uint16_t x16 = x, y16 = y, w16 = cw, h16 = ch;
    if (!clip_rect(&x16, &y16, &w16, &h16)) return;
    cw = (uint8_t)w16;
    ch = (uint8_t)h16;

    for (uint8_t row = 0; row < ch; row++) {
        uint8_t base_row = row / scale;
        for (uint8_t col = 0; col < cw; col++) {
            uint8_t base_col = col / scale;
            uint8_t pixel_on = 0;
            if (base_col < 5 && base_row < 7) {
                pixel_on = (font_5x7[idx][base_col] >> base_row) & 0x01;
            }
            fb_set(x + col, y + row, pixel_on ? fg : bg);
        }
    }
}

void st7735_draw_text(uint16_t x, uint16_t y, const char *str, font_size_t font, color_t fg, color_t bg) {
    if (!str) return;
    uint8_t cw, ch;
    font_get_size(font, &cw, &ch);

    while (*str) {
        if (x + cw > LCD_WIDTH) break;
        st7735_draw_char(x, y, *str, font, fg, bg);
        x += cw;
        str++;
    }
}

/* ================================================================
 * 位图
 * ================================================================ */
void st7735_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data) {
    if (!clip_rect(&x, &y, &w, &h) || !data) return;
    /* RGB565 逐像素写入帧缓冲 */
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint32_t off = (uint32_t)(row * w + col) * 2;
            color_t c = ((color_t)data[off] << 8) | data[off + 1];
            fb_set(x + col, y + row, c);
        }
    }
}

/* ================================================================
 * 背光 PWM
 * ================================================================ */
void st7735_set_backlight_pwm(uint16_t duty) {
    if (duty > 1000) duty = 1000;
    __HAL_TIM_SET_COMPARE(&htim3, LCD_BL_CHANNEL, duty);
}

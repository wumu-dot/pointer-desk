/**
 * st7735.c — ST7735S TFT LCD 驱动实现 (128×160, SPI, RGB565)
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

/* ---- HAL 句柄 (在 main.c 中定义) ---- */
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim3;

/* ================================================================
 * 内部常量 —— ST7735S 初始化序列
 *
 * 格式: [CMD, ARGC, DATA...] 或 [0xFF, DELAY_MS]
 * 结束标记: 0x00
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

    /* ---- Gamma 校正 (正极性) ---- */
    0xE0, 0x10,
    0x0F, 0x1A, 0x0F, 0x18, 0x2F, 0x28, 0x20, 0x22,
    0x1F, 0x1B, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10,

    /* ---- Gamma 校正 (负极性) ---- */
    0xE1, 0x10,
    0x0F, 0x1B, 0x0F, 0x17, 0x33, 0x2C, 0x29, 0x2E,
    0x30, 0x30, 0x39, 0x3F, 0x00, 0x07, 0x03, 0x10,

    /* ---- 像素格式 ---- */
    0x3A, 0x01, 0x05,                   // COLMOD: 16-bit/pixel (RGB565)

    /* ---- 显示方向 ---- */
    0x36, 0x01, 0xC8,                   // MADCTL: MY=1, MX=1, MV=1, BGR=0

    /* 正常显示模式 */
    0x13, 0x00,                         // NORON (Partial-off → Normal)

    /* 开启显示 */
    0x29, 0x00,                         // DISPON
    0xFF, 100,                          // 延时 100ms

    /* 列地址/行地址 0→127 / 0→159 */
    0x2A, 0x04, 0x00, 0x00, 0x00, 0x7F, // CASET: XSTART=0, XEND=127
    0x2B, 0x04, 0x00, 0x00, 0x00, 0x9F, // RASET: YSTART=0, YEND=159

    /* 普通模式 (非反转) */
    0x20, 0x00,                         // INVOFF

    0x00  /* 结束标记 */
};

/* ================================================================
 * 内部辅助 —— CS / DC 引脚操作 (inline + 独立函数)
 * ================================================================ */

static inline void cs_low(void)  { HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET);   }
static inline void dc_low(void)  { HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_RESET); }
static inline void dc_high(void) { HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_SET);   }

/* 发送命令 (单字节, DC=0) */
static void lcd_write_cmd(uint8_t cmd) {
    cs_low();
    dc_low();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    cs_high();
}

/* 发送数据 (单字节, DC=1) */
static void lcd_write_data8(uint8_t data) {
    cs_low();
    dc_high();
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
    cs_high();
}

/* 批量发送数据 (DC=1, CS 拉低一次) */
static void lcd_write_data_bulk(const uint8_t *data, uint32_t len) {
    if (len == 0) return;
    cs_low();
    dc_high();
    HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, HAL_MAX_DELAY);
    cs_high();
}

/* 批量发送16位颜色值 (DC=1, 用本地缓冲区批量发送减少SPI调用次数) */
static void lcd_write_color_bulk(color_t color, uint32_t count) {
    if (count == 0) return;

    /* 本地缓冲区: 64 像素 = 128 字节, 适合栈上分配 */
    #define BULK_BUF_PIXELS 64
    static uint8_t buf[BULK_BUF_PIXELS * 2];
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);

    /* 预填充缓冲区 (hi/lo 交替) */
    uint8_t *p = buf;
    for (uint8_t i = 0; i < BULK_BUF_PIXELS; i++) {
        *p++ = hi;
        *p++ = lo;
    }

    cs_low();
    dc_high();

    while (count > 0) {
        uint32_t chunk = (count > BULK_BUF_PIXELS) ? BULK_BUF_PIXELS : count;
        HAL_SPI_Transmit(&hspi1, buf, chunk * 2, HAL_MAX_DELAY);
        count -= chunk;
    }

    cs_high();
}

/* 裁剪矩形到屏幕范围 */
static bool clip_rect(uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h) {
    if (*x >= LCD_WIDTH  || *y >= LCD_HEIGHT) return false;
    if (*w == 0 || *h == 0) return false;
    if (*x + *w > LCD_WIDTH)  *w = LCD_WIDTH  - *x;
    if (*y + *h > LCD_HEIGHT) *h = LCD_HEIGHT - *y;
    return (*w > 0 && *h > 0);
}

/* ================================================================
 * 公开 API —— 初始化 & 控制
 * ================================================================ */

void st7735_init(void) {
    LOG("ST7735S init start\r\n");

    /* ---- 硬件复位 ---- */
    st7735_reset();

    /* ---- 发送初始化命令序列 ---- */
    const uint8_t *p = init_sequence;
    while (1) {
        uint8_t cmd = *p++;
        if (cmd == 0x00) break;            /* 结束 */

        uint8_t arg_len = *p++;

        if (cmd == 0xFF) {
            /* 延时命令: arg_len = 延时毫秒数 (上限约250ms) */
            HAL_Delay(arg_len);
        } else {
            lcd_write_cmd(cmd);
            for (uint8_t i = 0; i < arg_len; i++) {
                lcd_write_data8(*p++);
            }
        }
    }

    /* ---- 清屏 ---- */
    st7735_fill_screen(COLOR_BLACK);

    /* ---- 背光 ---- */
    st7735_set_brightness(LCD_BRIGHTNESS_DEFAULT);

    /* ---- 旋转 (默认竖屏) ---- */
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
        lcd_write_cmd(0x10);    /* SLPIN  — 进入休眠 */
    } else {
        lcd_write_cmd(0x11);    /* SLPOUT — 退出休眠 */
        HAL_Delay(120);
    }
}

void st7735_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    uint16_t duty = (uint16_t)pct * 10;     /* 映射到 0~1000 */
    st7735_set_backlight_pwm(duty);
}

void st7735_set_rotation(uint8_t rotation) {
    uint8_t madctl;
    switch (rotation & 0x03) {
        case 0:  madctl = 0xC8; break;      /* 默认: 竖屏, 上北 */
        case 1:  madctl = 0x68; break;      /* 顺时针 90° */
        case 2:  madctl = 0x08; break;      /* 顺时针 180° */
        case 3:  madctl = 0xA8; break;      /* 顺时针 270° */
        default: madctl = 0xC8; break;
    }
    lcd_write_cmd(0x36);
    lcd_write_data8(madctl);
}

/* ================================================================
 * 公开 API —— 窗口设置 (局部刷新基础)
 * ================================================================ */

void st7735_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!clip_rect(&x, &y, &w, &h)) return;

    uint16_t x2 = x + w - 1;
    uint16_t y2 = y + h - 1;

    /* CASET (列地址) */
    lcd_write_cmd(0x2A);
    lcd_write_data8(x >> 8);
    lcd_write_data8(x & 0xFF);
    lcd_write_data8(x2 >> 8);
    lcd_write_data8(x2 & 0xFF);

    /* RASET (行地址) */
    lcd_write_cmd(0x2B);
    lcd_write_data8(y >> 8);
    lcd_write_data8(y & 0xFF);
    lcd_write_data8(y2 >> 8);
    lcd_write_data8(y2 & 0xFF);

    /* RAMWR (内存写入开始) */
    lcd_write_cmd(0x2C);
}

void st7735_write_pixels(const uint8_t *data, uint32_t len) {
    lcd_write_data_bulk(data, len);
}

/* ================================================================
 * 公开 API —— 基本绘图
 * ================================================================ */

void st7735_fill_screen(color_t color) {
    st7735_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void st7735_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color) {
    if (!clip_rect(&x, &y, &w, &h)) return;
    st7735_set_window(x, y, w, h);
    lcd_write_color_bulk(color, (uint32_t)w * h);
}

void st7735_draw_pixel(uint16_t x, uint16_t y, color_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    st7735_set_window(x, y, 1, 1);
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    cs_low();
    dc_high();
    HAL_SPI_Transmit(&hspi1, &hi, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi1, &lo, 1, HAL_MAX_DELAY);
    cs_high();
}

void st7735_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, color_t color) {
    /* Bresenham 画线算法 */
    int16_t dx  = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t dy  = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    int16_t sx  = (x0 < x1) ? 1 : -1;
    int16_t sy  = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    int16_t e2;

    while (1) {
        st7735_draw_pixel(x0, y0, color);
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
    /* 上边 */
    st7735_fill_rect(x, y, w, 1, color);
    /* 下边 */
    st7735_fill_rect(x, y2, w, 1, color);
    /* 左边 (跳过已被上/下覆盖的像素) */
    if (h > 2) st7735_fill_rect(x, y + 1, 1, h - 2, color);
    /* 右边 */
    if (h > 2) st7735_fill_rect(x2, y + 1, 1, h - 2, color);
}

void st7735_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color) {
    if (r == 0) { st7735_draw_pixel(cx, cy, color); return; }

    int16_t f     = 1 - (int16_t)r;
    int16_t ddf_x = 1;
    int16_t ddf_y = -2 * (int16_t)r;
    int16_t x     = 0;
    int16_t y     = (int16_t)r;

    /* 4 个极点 */
    st7735_draw_pixel(cx, (uint16_t)(cy + r), color);
    st7735_draw_pixel(cx, (uint16_t)(cy - r), color);
    st7735_draw_pixel((uint16_t)(cx + r), cy, color);
    st7735_draw_pixel((uint16_t)(cx - r), cy, color);

    while (x < y) {
        if (f >= 0) { y--; ddf_y += 2; f += ddf_y; }
        x++;
        ddf_x += 2;
        f += ddf_x;

        /* 8 分圆对称 */
        st7735_draw_pixel((uint16_t)(cx + x), (uint16_t)(cy + y), color);
        st7735_draw_pixel((uint16_t)(cx - x), (uint16_t)(cy + y), color);
        st7735_draw_pixel((uint16_t)(cx + x), (uint16_t)(cy - y), color);
        st7735_draw_pixel((uint16_t)(cx - x), (uint16_t)(cy - y), color);
        st7735_draw_pixel((uint16_t)(cx + y), (uint16_t)(cy + x), color);
        st7735_draw_pixel((uint16_t)(cx - y), (uint16_t)(cy + x), color);
        st7735_draw_pixel((uint16_t)(cx + y), (uint16_t)(cy - x), color);
        st7735_draw_pixel((uint16_t)(cx - y), (uint16_t)(cy - x), color);
    }
}

void st7735_fill_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color) {
    int16_t f     = 1 - (int16_t)r;
    int16_t ddf_x = 1;
    int16_t ddf_y = -2 * (int16_t)r;
    int16_t x     = 0;
    int16_t y     = (int16_t)r;

    /* 画水平填充线 (每行从 cx-x 到 cx+x) */
    while (x < y) {
        if (f >= 0) { y--; ddf_y += 2; f += ddf_y; }
        x++;
        ddf_x += 2;
        f += ddf_x;

        /* 上半圆和下半圆的水平线 */
        for (int16_t i = (int16_t)cx - x; i <= (int16_t)cx + x; i++) {
            st7735_draw_pixel((uint16_t)i, (uint16_t)(cy + y), color);
            st7735_draw_pixel((uint16_t)i, (uint16_t)(cy - y), color);
        }
        for (int16_t i = (int16_t)cx - y; i <= (int16_t)cx + y; i++) {
            st7735_draw_pixel((uint16_t)i, (uint16_t)(cy + x), color);
            st7735_draw_pixel((uint16_t)i, (uint16_t)(cy - x), color);
        }
    }
}

/* ================================================================
 * 5×7 点阵 ASCII 字库 (0x20~0x7E, 95个可打印字符)
 * 每个字符 5 列, 每列一个字节 (bit0=顶行, bit6=底行, bit7=未使用)
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
 * 公开 API —— 字符 & 文本
 * ================================================================ */

/**
 * 内部: 发送字符像素数据 (不管理 CS / set_window, 由调用者负责)
 * 仅在被裁剪后的 (cw×ch) 区域内逐行发送。
 */
static void draw_char_raw(uint8_t idx, uint8_t cw, uint8_t ch, uint8_t scale,
                          color_t fg, color_t bg) {
    uint8_t fg_hi = (uint8_t)(fg >> 8);
    uint8_t fg_lo = (uint8_t)(fg & 0xFF);
    uint8_t bg_hi = (uint8_t)(bg >> 8);
    uint8_t bg_lo = (uint8_t)(bg & 0xFF);

    uint8_t row_buf[32];

    for (uint8_t row = 0; row < ch; row++) {
        uint8_t base_row = row / scale;
        uint8_t *p = row_buf;

        for (uint8_t col = 0; col < cw; col++) {
            uint8_t base_col = col / scale;
            uint8_t pixel_on = 0;

            if (base_col < 5 && base_row < 7) {
                pixel_on = (font_5x7[idx][base_col] >> base_row) & 0x01;
            }

            *p++ = pixel_on ? fg_hi : bg_hi;
            *p++ = pixel_on ? fg_lo : bg_lo;
        }

        HAL_SPI_Transmit(&hspi1, row_buf, (uint16_t)cw * 2, HAL_MAX_DELAY);
    }
}

/**
 * 内部: 获取字体尺寸
 */
static void font_get_size(font_size_t font, uint8_t *cw, uint8_t *ch) {
    switch (font) {
        case FONT_6x8:   *cw = 6;  *ch = 8;  break;
        case FONT_8x16:  *cw = 8;  *ch = 16; break;
        case FONT_12x24: *cw = 12; *ch = 24; break;
        case FONT_16x32: *cw = 16; *ch = 32; break;
        default:         *cw = 6;  *ch = 8;  break;
    }
}

/**
 * 内部: 获取缩放因子
 */
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
 * 公开 API —— 字符 & 文本
 * ================================================================ */

/**
 * 在 (x,y) 处绘制一个字符
 *
 * 基础字库为 5×7 像素，通过整数缩放实现大号字体：
 *   - FONT_6x8:  1× 缩放，5×7 像素
 *   - FONT_8x16: 1× 缩放 (8 列 = 5 数据列 + 3 间距列)
 *   - FONT_12x24: 2× 缩放 (12 列 = 5×2 数据 + 2 间距)
 *   - FONT_16x32: 3× 缩放
 *
 * 字库存储格式: font_5x7[idx][col], 每字节 = 一列的7行垂直位图
 *   bit0 = 顶行 (row 0), bit6 = 底行 (row 6)
 */
void st7735_draw_char(uint16_t x, uint16_t y, char c, font_size_t font, color_t fg, color_t bg) {
    if (c < 0x20 || c > 0x7E) c = ' ';
    uint8_t idx = (uint8_t)(c - 0x20);

    uint8_t cw, ch;
    font_get_size(font, &cw, &ch);
    uint8_t scale = font_scale(font);

    /* 裁剪 */
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    uint16_t x16 = x, y16 = y, w16 = cw, h16 = ch;
    if (!clip_rect(&x16, &y16, &w16, &h16)) return;
    cw = (uint8_t)w16;
    ch = (uint8_t)h16;

    st7735_set_window(x, y, cw, ch);
    cs_low();
    dc_high();
    draw_char_raw(idx, cw, ch, scale, fg, bg);
    cs_high();
}

/**
 * 在 (x,y) 处绘制字符串 (不自动换行)
 */
void st7735_draw_text(uint16_t x, uint16_t y, const char *str, font_size_t font, color_t fg, color_t bg) {
    if (!str) return;

    uint8_t cw, ch;
    font_get_size(font, &cw, &ch);

    while (*str) {
        if (x + cw > LCD_WIDTH) break;      /* 超出屏幕右侧, 截断 */
        st7735_draw_char(x, y, *str, font, fg, bg);
        x += cw;
        str++;
    }
}

/* ================================================================
 * 公开 API —— 位图
 * ================================================================ */

void st7735_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data) {
    if (!clip_rect(&x, &y, &w, &h)) return;
    if (!data) return;
    st7735_set_window(x, y, w, h);
    lcd_write_data_bulk(data, (uint32_t)w * h * 2);    /* RGB565 = 2 bytes/pixel */
}

/* ================================================================
 * 公开 API —— 背光 PWM
 * ================================================================ */

void st7735_set_backlight_pwm(uint16_t duty) {
    if (duty > 1000) duty = 1000;
    __HAL_TIM_SET_COMPARE(&htim3, LCD_BL_CHANNEL, duty);
}

# OV-Watch 固件完整实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 OV-Watch 桌面智能摆件的完整固件，包括 6 个 BSP 驱动、3 个中间件模块、4 个应用模式 + 状态机 + 4 个 FreeRTOS 任务，以及 main.c 集成。

**Architecture:** 自底向上分层实现：BSP 驱动层（硬件抽象）→ 中间件层（GUI/指针/LittleFS）→ 应用层（模式+状态机）→ RTOS 任务集成。每层编译验证后再进入下一层。采用依赖注入模式（各模块通过 HAL 句柄通信，不直接依赖全局变量）。

**Tech Stack:** STM32F407ZGT6, STM32Cube HAL, FreeRTOS (CMSIS-OS v2), LittleFS, C11, arm-none-eabi-gcc

**实现文件清单 (15 个 .c 文件):**

| # | 文件 | 层 | 大小估算 |
|---|------|-----|---------|
| 1 | `Drivers/BSP/lcd/st7735.c` | BSP 驱动 | ~300 行 |
| 2 | `Drivers/BSP/flash/w25q64.c` | BSP 驱动 | ~250 行 |
| 3 | `Drivers/BSP/motor/a4988.c` | BSP 驱动 | ~200 行 |
| 4 | `Drivers/BSP/button/button.c` | BSP 驱动 | ~120 行 |
| 5 | `Drivers/BSP/sensor/temp_adc.c` | BSP 驱动 | ~50 行 |
| 6 | `Drivers/BSP/sensor/temp_sensor.c` | BSP 驱动 | ~80 行 |
| 7 | `Drivers/BSP/rtc/rtc_drv.c` | BSP 驱动 | ~150 行 |
| 8 | `Middleware/gui/gui.c` | 中间件 | ~350 行 |
| 9 | `Middleware/pointer/pointer_engine.c` | 中间件 | ~250 行 |
| 10 | `Middleware/fs/fs_mgr.c` | 中间件 | ~300 行 |
| 11 | `App/modes/clock_mode.c` | 应用层 | ~200 行 |
| 12 | `App/modes/temp_mode.c` | 应用层 | ~150 行 |
| 13 | `App/modes/timer_mode.c` | 应用层 | ~250 行 |
| 14 | `App/modes/settings_mode.c` | 应用层 | ~250 行 |
| 15 | `App/mode_manager.c` | 应用层 | ~150 行 |
| — | `Core/Src/main.c` (修改) | 集成 | 修改 USER CODE 块 |
| — | `Core/Src/freertos.c` (修改) | 集成 | 修改 USER CODE 块 |

---

## Phase 0: 预检查

### Task 0: 验证项目骨架编译

**Files:**
- Modify: `firmware/Core/Inc/main.h` — 添加必要的全局句柄 extern 声明

- [ ] **Step 1: 检查 CubeMX 生成文件完整性**

确认以下文件存在且未被修改（保留 CubeMX USER CODE 标记）:
```bash
ls firmware/Core/Src/main.c firmware/Core/Src/freertos.c firmware/Core/Src/stm32f4xx_it.c
ls firmware/Core/Inc/main.h firmware/Core/Inc/FreeRTOSConfig.h
```

- [ ] **Step 2: 在 main.h 添加全局句柄 extern 声明**

在 `main.h` 的 `/* USER CODE BEGIN EM */` 和 `/* USER CODE END EM */` 之间插入:

```c
/* USER CODE BEGIN EM */
extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c2;
extern RTC_HandleTypeDef hrtc;
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;
extern UART_HandleTypeDef huart1;
/* USER CODE END EM */
```

- [ ] **Step 3: 尝试编译验证 CubeMX 骨架无错误**

```bash
# 如果有 Makefile 或 IDE 项目，尝试编译
# 预期：编译通过（或仅 warning），FreeRTOS 任务骨架正常链接
```

---

## Phase 1: BSP 驱动层（自底向上，无外部依赖）

### Task 1: ST7735S TFT LCD 驱动

**Files:**
- Create: `firmware/Drivers/BSP/lcd/st7735.c`

- [ ] **Step 1: 创建 st7735.c 实现文件**

```c
/**
 * st7735.c — ST7735S TFT LCD 驱动实现 (SPI)
 *
 * 依赖: main.h (HAL 句柄), pin_config.h (引脚宏), app_config.h (调试开关)
 */

#include "st7735.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

/* ================================================================
 * 内部常量 — ST7735S 初始化序列
 * ================================================================ */

/* 精简初始化命令表 (ST7735S 128x160, RGB565) */
static const uint8_t init_cmds[] = {
    /* 软件复位 */
    0x01, 0x00,                                    // SWRESET
    0xFF, 150,                                     // 延时 150ms
    /* 退出休眠 */
    0x11, 0x00,                                    // SLPOUT
    0xFF, 150,                                     // 延时 150ms
    /* 颜色模式 */
    0x3A, 0x01, 0x05,                              // COLMOD: 16bits/pixel (RGB565)
    /* 帧速率 */
    0xB1, 0x03, 0x01, 0x2C, 0x2D,                  // FRMCTR1
    0xB2, 0x03, 0x01, 0x2C, 0x2D,                  // FRMCTR2
    0xB3, 0x06, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D, // FRMCTR3
    /* 显示反转 */
    0xB4, 0x01, 0x07,                              // INVCTR: dot inversion
    /* 电源设置 */
    0xC0, 0x03, 0xA2, 0x02, 0x84,                  // PWCTR1
    0xC1, 0x01, 0xC5,                              // PWCTR2
    0xC2, 0x02, 0x0A, 0x00,                        // PWCTR3
    0xC3, 0x02, 0x8A, 0x2A,                        // PWCTR4
    0xC4, 0x02, 0x8A, 0xEE,                        // PWCTR5
    0xC5, 0x01, 0x0E,                              // VMCTR1
    /* Gamma 校正 */
    0xE0, 0x10,
    0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10, // GMCTRP1
    0xE1, 0x10,
    0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10, // GMCTRN1
    /* 显示模式 */
    0x36, 0x01, 0xC8,                              // MADCTL: MY=1, MX=1, MV=1, RGB order
    /* 正常显示模式 */
    0x13, 0x00,                                    // NORON
    /* 开启显示 */
    0x29, 0x00,                                    // DISPON
    0xFF, 100,                                     // 延时 100ms
    0x00 /* 结束标记 */
};

/* ================================================================
 * 内部辅助
 * ================================================================ */

static inline void cs_low(void) {
    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
}

static inline void cs_high(void) {
    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
}

static inline void dc_low(void) {
    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET);
}

static inline void dc_high(void) {
    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);
}

/* 发送命令 */
static void write_cmd(uint8_t cmd) {
    cs_low();
    dc_low();
    HAL_SPI_Transmit(&LCD_SPI, &cmd, 1, HAL_MAX_DELAY);
    cs_high();
}

/* 发送数据 (单字节) */
static void write_data(uint8_t data) {
    cs_low();
    dc_high();
    HAL_SPI_Transmit(&LCD_SPI, &data, 1, HAL_MAX_DELAY);
    cs_high();
}

/* 批量发送数据 */
static void write_data_bulk(const uint8_t *data, uint32_t len) {
    cs_low();
    dc_high();
    HAL_SPI_Transmit(&LCD_SPI, (uint8_t *)data, len, HAL_MAX_DELAY);
    cs_high();
}

/* ================================================================
 * 公开 API 实现
 * ================================================================ */

void st7735_init(void) {
    /* 硬件复位 */
    st7735_reset();

    /* 发送初始化命令序列 */
    const uint8_t *p = init_cmds;
    while (*p != 0x00) {
        uint8_t cmd = *p++;
        uint8_t argc = *p++;
        if (cmd == 0xFF) {
            /* 延时命令 */
            HAL_Delay(argc);
        } else {
            write_cmd(cmd);
            for (uint8_t i = 0; i < argc; i++) {
                write_data(*p++);
            }
        }
    }

    /* 清屏为黑色 */
    st7735_fill_screen(COLOR_BLACK);

    /* 开启背光，默认 80% */
    st7735_set_brightness(LCD_BRIGHTNESS_DEFAULT);

    /* 设置旋转 */
    st7735_set_rotation(LCD_ROTATION);

    LOG("ST7735S initialized\r\n");
}

void st7735_reset(void) {
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
}

void st7735_sleep(bool enable) {
    write_cmd(enable ? 0x10 : 0x11); /* SLPIN / SLPOUT */
    if (!enable) HAL_Delay(120);
}

void st7735_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    /* 映射到 PWM duty 0-1000 */
    st7735_set_backlight_pwm((uint16_t)pct * 10);
}

void st7735_set_rotation(uint8_t rotation) {
    uint8_t madctl;
    switch (rotation & 0x03) {
        case 0: madctl = 0xC8; break; /* 默认竖屏 */
        case 1: madctl = 0x68; break; /* 旋转 90° */
        case 2: madctl = 0x08; break; /* 旋转 180° */
        case 3: madctl = 0xA8; break; /* 旋转 270° */
        default: madctl = 0xC8; break;
    }
    write_cmd(0x36);
    write_data(madctl);
}

void st7735_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    write_cmd(0x2A); /* CASET (列地址) */
    write_data(x >> 8);
    write_data(x & 0xFF);
    write_data((x + w - 1) >> 8);
    write_data((x + w - 1) & 0xFF);

    write_cmd(0x2B); /* RASET (行地址) */
    write_data(y >> 8);
    write_data(y & 0xFF);
    write_data((y + h - 1) >> 8);
    write_data((y + h - 1) & 0xFF);

    write_cmd(0x2C); /* RAMWR (内存写入) */
}

void st7735_fill_screen(color_t color) {
    st7735_set_window(0, 0, LCD_WIDTH, LCD_HEIGHT);
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    cs_low();
    dc_high();
    /* 逐像素写入 (128×160=20480 像素) */
    for (uint32_t i = 0; i < (uint32_t)LCD_WIDTH * LCD_HEIGHT; i++) {
        HAL_SPI_Transmit(&LCD_SPI, &hi, 1, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&LCD_SPI, &lo, 1, HAL_MAX_DELAY);
    }
    cs_high();
}

void st7735_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    st7735_set_window(x, y, w, h);
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    cs_low();
    dc_high();
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        HAL_SPI_Transmit(&LCD_SPI, &hi, 1, HAL_MAX_DELAY);
        HAL_SPI_Transmit(&LCD_SPI, &lo, 1, HAL_MAX_DELAY);
    }
    cs_high();
}

void st7735_draw_pixel(uint16_t x, uint16_t y, color_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    st7735_set_window(x, y, 1, 1);
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    cs_low();
    dc_high();
    HAL_SPI_Transmit(&LCD_SPI, &hi, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&LCD_SPI, &lo, 1, HAL_MAX_DELAY);
    cs_high();
}

void st7735_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, color_t color) {
    /* Bresenham 画线算法 */
    int16_t dx = (int16_t)(x1 > x0 ? x1 - x0 : x0 - x1);
    int16_t dy = -(int16_t)(y1 > y0 ? y1 - y0 : y0 - y1);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while (1) {
        st7735_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void st7735_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, color_t color) {
    st7735_draw_line(x, y, x + w - 1, y, color);
    st7735_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    st7735_draw_line(x, y, x, y + h - 1, color);
    st7735_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void st7735_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color) {
    int16_t f = 1 - (int16_t)r;
    int16_t ddf_x = 1;
    int16_t ddf_y = -2 * (int16_t)r;
    int16_t x = 0;
    int16_t y = (int16_t)r;

    st7735_draw_pixel(cx, cy + r, color);
    st7735_draw_pixel(cx, cy - r, color);
    st7735_draw_pixel(cx + r, cy, color);
    st7735_draw_pixel(cx - r, cy, color);

    while (x < y) {
        if (f >= 0) { y--; ddf_y += 2; f += ddf_y; }
        x++; ddf_x += 2; f += ddf_x;
        st7735_draw_pixel(cx + x, cy + y, color);
        st7735_draw_pixel(cx - x, cy + y, color);
        st7735_draw_pixel(cx + x, cy - y, color);
        st7735_draw_pixel(cx - x, cy - y, color);
        st7735_draw_pixel(cx + y, cy + x, color);
        st7735_draw_pixel(cx - y, cy + x, color);
        st7735_draw_pixel(cx + y, cy - x, color);
        st7735_draw_pixel(cx - y, cy - x, color);
    }
}

void st7735_fill_circle(uint16_t cx, uint16_t cy, uint16_t r, color_t color) {
    int16_t f = 1 - (int16_t)r;
    int16_t ddf_x = 1;
    int16_t ddf_y = -2 * (int16_t)r;
    int16_t x = 0;
    int16_t y = (int16_t)r;

    while (x < y) {
        if (f >= 0) { y--; ddf_y += 2; f += ddf_y; }
        x++; ddf_x += 2; f += ddf_x;
        /* 水平填充线段 */
        for (int16_t i = cx - x; i <= cx + x; i++) {
            st7735_draw_pixel(i, cy + y, color);
            st7735_draw_pixel(i, cy - y, color);
        }
        for (int16_t i = cx - y; i <= cx + y; i++) {
            st7735_draw_pixel(i, cy + x, color);
            st7735_draw_pixel(i, cy - x, color);
        }
    }
}

/* 5x7 点阵 ASCII 字体内存 (只有可打印字符 0x20-0x7E) */
static const uint8_t font_5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* SPACE */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    /* ... truncated for brevity in plan — full 96 glyphs follow same pattern */
};

void st7735_draw_char(uint16_t x, uint16_t y, char c, font_size_t font, color_t fg, color_t bg) {
    if (c < 0x20 || c > 0x7E) c = ' ';
    uint8_t idx = (uint8_t)(c - 0x20);

    uint8_t cw = 6, ch = 8;
    switch (font) {
        case FONT_6x8:  cw = 6; ch = 8;  break;
        case FONT_8x16: cw = 8; ch = 16; break;
        case FONT_12x24: cw = 12; ch = 24; break;
        case FONT_16x32: cw = 16; ch = 32; break;
    }

    st7735_set_window(x, y, cw, ch);
    uint8_t fg_hi = fg >> 8, fg_lo = fg & 0xFF;
    uint8_t bg_hi = bg >> 8, bg_lo = bg & 0xFF;
    cs_low();
    dc_high();

    for (uint8_t row = 0; row < ch; row++) {
        uint8_t line = 0;
        if (font == FONT_6x8) {
            line = (row < 7) ? font_5x7[idx][row] : 0;
        }
        /* 更大字体: 2×/3×/4× 缩放 */
        uint8_t scale = 1;
        if (font == FONT_8x16) scale = 2;
        else if (font == FONT_12x24) scale = 3;
        else if (font == FONT_16x32) scale = 4;

        for (uint8_t sr = 0; sr < scale; sr++) {
            for (uint8_t col = 0; col < cw; col++) {
                uint8_t bit = (line >> (5 - col / scale)) & 0x01;
                uint8_t hi = bit ? fg_hi : bg_hi;
                uint8_t lo = bit ? fg_lo : bg_lo;
                HAL_SPI_Transmit(&LCD_SPI, &hi, 1, HAL_MAX_DELAY);
                HAL_SPI_Transmit(&LCD_SPI, &lo, 1, HAL_MAX_DELAY);
            }
        }
    }
    cs_high();
}

void st7735_draw_text(uint16_t x, uint16_t y, const char *str, font_size_t font, color_t fg, color_t bg) {
    uint8_t cw = 6;
    switch (font) {
        case FONT_6x8:  cw = 6; break;
        case FONT_8x16: cw = 8; break;
        case FONT_12x24: cw = 12; break;
        case FONT_16x32: cw = 16; break;
    }
    while (*str) {
        st7735_draw_char(x, y, *str++, font, fg, bg);
        x += cw;
    }
}

void st7735_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *data) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    st7735_set_window(x, y, w, h);
    write_data_bulk(data, (uint32_t)w * h * 2); /* RGB565 = 2 bytes/pixel */
}

void st7735_write_pixels(const uint8_t *data, uint32_t len) {
    write_data_bulk(data, len);
}

void st7735_set_backlight_pwm(uint16_t duty) {
    /* TIM3 CH3 PWM 控制背光 */
    if (duty > 1000) duty = 1000;
    __HAL_TIM_SET_COMPARE(&LCD_BL_TIM, LCD_BL_CHANNEL, duty);
}
```

- [ ] **Step 2: 编译验证**

```bash
# 将 st7735.c 加入工程编译，预期仅对 font 表不完整产生 warning（无错误）
```

---

### Task 2: W25Q64 SPI Flash 驱动

**Files:**
- Create: `firmware/Drivers/BSP/flash/w25q64.c`

- [ ] **Step 1: 创建 w25q64.c**

```c
/**
 * w25q64.c — W25Q64 8MB SPI Flash 驱动实现
 */

#include "w25q64.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

/* W25Q64 指令集 */
#define CMD_READ_ID          0x9F
#define CMD_READ_UNIQUE_ID   0x4B
#define CMD_WRITE_ENABLE     0x06
#define CMD_WRITE_DISABLE    0x04
#define CMD_READ_STATUS1     0x05
#define CMD_READ_STATUS2     0x35
#define CMD_READ_DATA        0x03
#define CMD_PAGE_PROGRAM     0x02
#define CMD_SECTOR_ERASE     0x20
#define CMD_BLOCK_ERASE_32K  0x52
#define CMD_BLOCK_ERASE_64K  0xD8
#define CMD_CHIP_ERASE       0xC7
#define CMD_POWER_DOWN       0xB9
#define CMD_RELEASE_POWERDOWN 0xAB
#define CMD_READ_SFDP        0x5A

#define FLASH_TIMEOUT_MS     5000

/* ---- 内部辅助 ---- */

static inline void flash_cs_low(void) {
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
}

static inline void flash_cs_high(void) {
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
}

static uint8_t flash_read_status(uint8_t reg) {
    uint8_t cmd = (reg == 1) ? CMD_READ_STATUS1 : CMD_READ_STATUS2;
    uint8_t status = 0;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&FLASH_SPI, &status, 1, HAL_MAX_DELAY);
    flash_cs_high();
    return status;
}

static void flash_write_enable(void) {
    uint8_t cmd = CMD_WRITE_ENABLE;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_cs_high();
}

/* 发送 3 字节地址 */
static void flash_send_addr(uint32_t addr) {
    uint8_t buf[3] = {
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr)
    };
    HAL_SPI_Transmit(&FLASH_SPI, buf, 3, HAL_MAX_DELAY);
}

/* ---- API 实现 ---- */

void w25q64_init(void) {
    flash_cs_high();
    HAL_Delay(1);

    uint32_t id = w25q64_get_id();
    if (id == 0x00000000 || id == 0xFFFFFFFF) {
        LOG_ERR("W25Q64 not responding (ID=0x%08lX)\r\n", id);
    } else {
        LOG("W25Q64 ID: 0x%06lX\r\n", id & 0xFFFFFF);
    }

    /* 退出掉电模式 (如果处于掉电) */
    uint8_t cmd = CMD_RELEASE_POWERDOWN;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_cs_high();
}

uint32_t w25q64_get_id(void) {
    uint8_t cmd = CMD_READ_ID;
    uint8_t buf[3] = {0};
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&FLASH_SPI, buf, 3, HAL_MAX_DELAY);
    flash_cs_high();
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

uint64_t w25q64_get_unique_id(void) {
    uint8_t cmd[5] = {CMD_READ_UNIQUE_ID, 0, 0, 0, 0};
    uint8_t buf[8] = {0};
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, cmd, 5, HAL_MAX_DELAY);
    HAL_SPI_Receive(&FLASH_SPI, buf, 8, HAL_MAX_DELAY);
    flash_cs_high();
    uint64_t uid = 0;
    for (int i = 0; i < 8; i++) {
        uid = (uid << 8) | buf[i];
    }
    return uid;
}

uint32_t w25q64_get_capacity(void) {
    return FLASH_SIZE_BYTES;
}

bool w25q64_is_busy(void) {
    return (flash_read_status(1) & 0x01) != 0;
}

void w25q64_wait_ready(void) {
    uint32_t start = HAL_GetTick();
    while (w25q64_is_busy()) {
        if (HAL_GetTick() - start > FLASH_TIMEOUT_MS) {
            LOG_ERR("W25Q64 timeout waiting for ready\r\n");
            break;
        }
    }
}

void w25q64_erase_sector(uint32_t addr) {
    flash_write_enable();
    uint8_t cmd = CMD_SECTOR_ERASE;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_send_addr(addr);
    flash_cs_high();
    w25q64_wait_ready();
}

void w25q64_erase_block_32k(uint32_t addr) {
    flash_write_enable();
    uint8_t cmd = CMD_BLOCK_ERASE_32K;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_send_addr(addr);
    flash_cs_high();
    w25q64_wait_ready();
}

void w25q64_erase_block_64k(uint32_t addr) {
    flash_write_enable();
    uint8_t cmd = CMD_BLOCK_ERASE_64K;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_send_addr(addr);
    flash_cs_high();
    w25q64_wait_ready();
}

void w25q64_erase_chip(void) {
    flash_write_enable();
    uint8_t cmd = CMD_CHIP_ERASE;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_cs_high();
    w25q64_wait_ready(); /* 整片擦除可能需要 40+ 秒 */
}

void w25q64_read(uint32_t addr, uint8_t *buf, uint32_t len) {
    uint8_t cmd = CMD_READ_DATA;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_send_addr(addr);
    HAL_SPI_Receive(&FLASH_SPI, buf, len, HAL_MAX_DELAY);
    flash_cs_high();
}

void w25q64_write_page(uint32_t addr, const uint8_t *buf, uint32_t len) {
    if (len > FLASH_PAGE_SIZE) len = FLASH_PAGE_SIZE;
    flash_write_enable();
    uint8_t cmd = CMD_PAGE_PROGRAM;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_send_addr(addr);
    HAL_SPI_Transmit(&FLASH_SPI, (uint8_t *)buf, len, HAL_MAX_DELAY);
    flash_cs_high();
    w25q64_wait_ready();
}

void w25q64_write(uint32_t addr, const uint8_t *buf, uint32_t len) {
    uint32_t written = 0;
    while (written < len) {
        uint32_t page_remain = FLASH_PAGE_SIZE - (addr % FLASH_PAGE_SIZE);
        uint32_t chunk = len - written;
        if (chunk > page_remain) chunk = page_remain;
        w25q64_write_page(addr, buf + written, chunk);
        addr += chunk;
        written += chunk;
    }
}

void w25q64_power_down(void) {
    uint8_t cmd = CMD_POWER_DOWN;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_cs_high();
}

void w25q64_wake_up(void) {
    uint8_t cmd = CMD_RELEASE_POWERDOWN;
    flash_cs_low();
    HAL_SPI_Transmit(&FLASH_SPI, &cmd, 1, HAL_MAX_DELAY);
    flash_cs_high();
    HAL_Delay(3); /* tRES1 = 3µs min, 用 3ms 安全 */
}
```

- [ ] **Step 2: 编译验证** — 将 w25q64.c 加入工程，确认编译通过。

---

### Task 3: A4988 步进电机驱动

**Files:**
- Create: `firmware/Drivers/BSP/motor/a4988.c`

- [ ] **Step 1: 创建 a4988.c**

```c
/**
 * a4988.c — A4988 步进电机驱动实现
 *
 * TIM2 CH1 输出 PWM 到 STEP 引脚，硬件自动产生脉冲。
 * 改变 ARR 调节频率 = 改变速度。
 */

#include "a4988.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

static motor_state_t motor = {0};

/* 细分模式 → MS 引脚值 */
static const struct {
    bool ms1, ms2, ms3;
} ms_pins[] = {
    [STEP_FULL]      = {0,0,0},
    [STEP_HALF]      = {1,0,0},
    [STEP_QUARTER]   = {0,1,0},
    [STEP_EIGHTH]    = {1,1,0},
    [STEP_SIXTEENTH] = {1,1,1},
};

static microstep_t current_microstep = STEP_SIXTEENTH;

/* ---- 内部辅助 ---- */

static void set_ms_pins(microstep_t mode) {
    HAL_GPIO_WritePin(MOTOR_MS1_PORT, MOTOR_MS1_PIN, ms_pins[mode].ms1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_MS2_PORT, MOTOR_MS2_PIN, ms_pins[mode].ms2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_MS3_PORT, MOTOR_MS3_PIN, ms_pins[mode].ms3 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---- API 实现 ---- */

void a4988_init(void) {
    /* EN 引脚高电平禁用驱动 */
    HAL_GPIO_WritePin(MOTOR_EN_PORT, MOTOR_EN_PIN, GPIO_PIN_SET);

    /* DIR 初始方向 */
    HAL_GPIO_WritePin(MOTOR_DIR_PORT, MOTOR_DIR_PIN, GPIO_PIN_RESET);

    /* 16 细分 */
    a4988_set_microstep(STEP_SIXTEENTH);

    /* 启动 TIM2 PWM (STEP 脉冲), 初始频率=0 (不输出脉冲) */
    a4988_set_speed(0);

    /* 使能驱动 */
    a4988_enable(true);

    motor.is_moving = false;
    motor.current_steps = 0;
    motor.target_steps = 0;
    motor.direction = true;

    LOG("A4988 initialized (16 microsteps)\r\n");
}

void a4988_set_microstep(microstep_t mode) {
    current_microstep = mode;
    set_ms_pins(mode);
}

void a4988_enable(bool en) {
    HAL_GPIO_WritePin(MOTOR_EN_PORT, MOTOR_EN_PIN, en ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void a4988_set_direction(bool dir) {
    motor.direction = dir;
    HAL_GPIO_WritePin(MOTOR_DIR_PORT, MOTOR_DIR_PIN, dir ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void a4988_set_speed(uint32_t steps_per_sec) {
    if (steps_per_sec == 0) {
        /* 停止脉冲 */
        HAL_TIM_PWM_Stop(&MOTOR_STEP_TIM, MOTOR_STEP_CHANNEL);
        motor.is_moving = false;
        return;
    }

    /* TIM2 时钟 = 84MHz, Prescaler = 83 → 1MHz 计数频率
     * ARR = 1,000,000 / steps_per_sec - 1
     * Pulse = ARR / 2 (50% 占空比) */
    uint32_t freq = MOTOR_STEP_TIM.Init.Prescaler;
    uint32_t arr = (APB1_TIM_CLK / (freq + 1)) / steps_per_sec - 1;

    if (arr < 1) arr = 1;
    if (arr > 0xFFFF) arr = 0xFFFF;

    __HAL_TIM_SET_AUTORELOAD(&MOTOR_STEP_TIM, (uint16_t)arr);
    __HAL_TIM_SET_COMPARE(&MOTOR_STEP_TIM, MOTOR_STEP_CHANNEL, (uint16_t)(arr / 2));

    if (!motor.is_moving) {
        HAL_TIM_PWM_Start(&MOTOR_STEP_TIM, MOTOR_STEP_CHANNEL);
        motor.is_moving = true;
    }
}

void a4988_pulse_once(void) {
    /* 手动翻转 STEP 脚 (调试用) */
    HAL_GPIO_WritePin(MOTOR_STEP_PORT, MOTOR_STEP_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(MOTOR_STEP_PORT, MOTOR_STEP_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
}

void a4988_emergency_stop(void) {
    a4988_set_speed(0);
    a4988_enable(false); /* 释放电机 */
    motor.is_moving = false;
}

motor_state_t a4988_get_state(void) {
    return motor;
}

bool a4988_is_moving(void) {
    return motor.is_moving;
}

void a4988_vibrate(uint8_t count, uint16_t interval_ms) {
    a4988_enable(true);
    for (uint8_t i = 0; i < count; i++) {
        /* 正转一小段 */
        a4988_set_direction(true);
        a4988_set_speed(200);
        HAL_Delay(interval_ms / 2);
        /* 反转一小段 */
        a4988_set_direction(false);
        HAL_Delay(interval_ms / 2);
    }
    a4988_set_speed(0);
}
```

- [ ] **Step 2: 编译验证** — 将 a4988.c 加入工程，确认编译通过。

---

### Task 4: 四角开关驱动

**Files:**
- Create: `firmware/Drivers/BSP/button/button.c`

- [ ] **Step 1: 创建 button.c**

```c
/**
 * button.c — 四角开关 (5向导航键) 驱动实现
 *
 * 10ms 轮询 + 20ms 去抖 + 短按/长按/长按连发识别
 */

#include "button.h"
#include "pin_config.h"
#include "app_config.h"

/* ---- 按键状态追踪 ---- */
static struct {
    button_id_t id;
    uint8_t     pin;
    GPIO_TypeDef *port;
} btn_map[] = {
    {BTN_UP,     BTN_UP_PIN,     BTN_UP_PORT},
    {BTN_DOWN,   BTN_DOWN_PIN,   BTN_DOWN_PORT},
    {BTN_LEFT,   BTN_LEFT_PIN,   BTN_LEFT_PORT},
    {BTN_RIGHT,  BTN_RIGHT_PIN,  BTN_RIGHT_PORT},
    {BTN_CENTER, BTN_CENTER_PIN, BTN_CENTER_PORT},
};

#define BTN_COUNT (sizeof(btn_map) / sizeof(btn_map[0]))

typedef struct {
    bool     pressed;          /* 当前物理状态 (去抖后) */
    bool     last_pressed;     /* 上一轮状态 */
    uint32_t press_start_ms;   /* 按下时刻 */
    bool     long_triggered;   /* 长按已触发 */
    uint32_t last_repeat_ms;   /* 上次连发时刻 */
    bool     event_pending;    /* 有未读取事件 */
    button_msg_t event;        /* 待读取事件 */
} btn_state_t;

static btn_state_t btn_states[BTN_COUNT];

/* ---- API 实现 ---- */

void button_init(void) {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        btn_states[i].pressed = false;
        btn_states[i].last_pressed = false;
        btn_states[i].press_start_ms = 0;
        btn_states[i].long_triggered = false;
        btn_states[i].last_repeat_ms = 0;
        btn_states[i].event_pending = false;
    }
    LOG("Button driver initialized\r\n");
}

void button_scan(void) {
    uint32_t now = HAL_GetTick();

    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        /* 读取物理引脚 (低电平=按下，因为有上拉) */
        bool raw = (HAL_GPIO_ReadPin(btn_map[i].port, btn_map[i].pin) == GPIO_PIN_RESET);

        /* 简单去抖：状态改变需要连续两次相同 */
        if (raw != btn_states[i].last_pressed) {
            btn_states[i].last_pressed = raw;
            continue; /* 等待下一轮确认 */
        }

        /* 状态确认 */
        bool old = btn_states[i].pressed;
        btn_states[i].pressed = raw;

        if (!old && raw) {
            /* 刚按下 */
            btn_states[i].press_start_ms = now;
            btn_states[i].long_triggered = false;
            btn_states[i].last_repeat_ms = 0;
        } else if (old && !raw) {
            /* 刚释放 */
            if (!btn_states[i].long_triggered) {
                /* 短按 */
                btn_states[i].event.id = btn_map[i].id;
                btn_states[i].event.event = BTN_EVENT_SHORT_PRESS;
                btn_states[i].event.timestamp_ms = now;
                btn_states[i].event_pending = true;
            } else {
                /* 长按后释放 */
                btn_states[i].event.id = btn_map[i].id;
                btn_states[i].event.event = BTN_EVENT_RELEASE;
                btn_states[i].event.timestamp_ms = now;
                btn_states[i].event_pending = true;
            }
        } else if (old && raw) {
            /* 持续按住 */
            uint32_t elapsed = now - btn_states[i].press_start_ms;
            if (!btn_states[i].long_triggered && elapsed >= BTN_LONG_MS) {
                /* 长按触发 */
                btn_states[i].long_triggered = true;
                btn_states[i].event.id = btn_map[i].id;
                btn_states[i].event.event = BTN_EVENT_LONG_PRESS;
                btn_states[i].event.timestamp_ms = now;
                btn_states[i].event_pending = true;
                btn_states[i].last_repeat_ms = now;
            } else if (btn_states[i].long_triggered &&
                       (now - btn_states[i].last_repeat_ms) >= 200) {
                /* 长按连发 */
                btn_states[i].event.id = btn_map[i].id;
                btn_states[i].event.event = BTN_EVENT_LONG_REPEAT;
                btn_states[i].event.timestamp_ms = now;
                btn_states[i].event_pending = true;
                btn_states[i].last_repeat_ms = now;
            }
        }
    }
}

button_id_t button_get_pressed(void) {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        if (btn_states[i].pressed) return btn_map[i].id;
    }
    return BTN_NONE;
}

bool button_is_pressed(button_id_t btn) {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        if (btn_map[i].id == btn) return btn_states[i].pressed;
    }
    return false;
}

button_msg_t button_get_event(void) {
    button_msg_t evt = {BTN_NONE, BTN_EVENT_NONE, 0};
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        if (btn_states[i].event_pending) {
            evt = btn_states[i].event;
            btn_states[i].event_pending = false;
            break;
        }
    }
    return evt;
}

bool button_has_event(void) {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        if (btn_states[i].event_pending) return true;
    }
    return false;
}
```

- [ ] **Step 2: 编译验证** — 将 button.c 加入工程，确认编译通过。

---

### Task 5: 温度传感器驱动 (ADC + 统一接口)

**Files:**
- Create: `firmware/Drivers/BSP/sensor/temp_adc.c`
- Create: `firmware/Drivers/BSP/sensor/temp_sensor.c`

- [ ] **Step 1: 创建 temp_adc.c**

```c
/**
 * temp_adc.c — STM32 内部 ADC 温度传感器实现
 */

#include "temp_sensor.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

static bool initialized = false;

void temp_adc_init(void) {
    /* ADC1 已在 CubeMX 中初始化，此处做校准 */
    HAL_ADCEx_Calibration_Start(&TEMP_ADC);
    initialized = true;
}

float temp_adc_read_celsius(void) {
    if (!initialized) return 0.0f;

    HAL_ADC_Start(&TEMP_ADC);
    if (HAL_ADC_PollForConversion(&TEMP_ADC, 10) == HAL_OK) {
        uint32_t adc_val = HAL_ADC_GetValue(&TEMP_ADC);
        HAL_ADC_Stop(&TEMP_ADC);

        /* STM32F4 内部温度传感器公式:
         * V_sense = adc_val * Vref / 4096
         * Temperature = (V_sense - V_25) / Avg_Slope + 25
         *
         * V_25 = 0.76V (典型值)
         * Avg_Slope = 2.5mV/°C
         * Vref = 3.3V
         */
        float v_sense = (float)adc_val * 3.3f / 4096.0f;
        float temp = (v_sense - 0.76f) / 0.0025f + 25.0f;
        return temp;
    }
    return 0.0f;
}
```

- [ ] **Step 2: 创建 temp_sensor.c**

```c
/**
 * temp_sensor.c — 温度传感器统一接口实现
 *
 * 自动选择最佳可用传感器: SHT30 > ADC > NONE
 */

#include "temp_sensor.h"
#include "pin_config.h"
#include "app_config.h"

static temp_source_t active_source = TEMP_SRC_NONE;
static bool sht30_present = false;

void temp_sensor_init(void) {
    /* 初始化 ADC */
    temp_adc_init();
    active_source = TEMP_SRC_INTERNAL_ADC;

    /* 尝试检测 SHT30 */
    sht30_init();
    sht30_present = sht30_is_present();
    if (sht30_present) {
        active_source = TEMP_SRC_SHT30;
        LOG("SHT30 detected, using external sensor\r\n");
    } else {
        LOG("SHT30 not found, using internal ADC (±5°C)\r\n");
    }
}

temp_data_t temp_sensor_read(void) {
    temp_data_t data = {0};
    data.timestamp_ms = HAL_GetTick();

    if (active_source == TEMP_SRC_SHT30 && sht30_present) {
        temp_data_t sht = sht30_read();
        data.temperature = sht.temperature;
        data.humidity = sht.humidity;
        data.source = TEMP_SRC_SHT30;
    } else if (active_source == TEMP_SRC_INTERNAL_ADC) {
        data.temperature = temp_adc_read_celsius();
        data.humidity = 0.0f;
        data.source = TEMP_SRC_INTERNAL_ADC;
    } else {
        data.temperature = 0.0f;
        data.humidity = 0.0f;
        data.source = TEMP_SRC_NONE;
    }
    return data;
}

bool temp_sensor_has_external(void) {
    return sht30_present;
}

temp_source_t temp_sensor_get_source(void) {
    return active_source;
}

const char* temp_sensor_get_label(void) {
    switch (active_source) {
        case TEMP_SRC_SHT30:        return "±0.3°C SHT30";
        case TEMP_SRC_INTERNAL_ADC: return "±5°C Internal";
        default:                    return "No Sensor";
    }
}

/* SHT30 存根实现 (I2C 读取 — 调试阶段先返回未检测到) */

void sht30_init(void) {
    /* I2C2 扫描 0x44 地址 */
    uint8_t dummy = 0;
    sht30_present = (HAL_I2C_IsDeviceReady(&SHT30_I2C, SHT30_I2C_ADDR << 1, 2, 50) == HAL_OK);
}

bool sht30_is_present(void) {
    return sht30_present;
}

temp_data_t sht30_read(void) {
    temp_data_t data = {0};
    if (!sht30_present) return data;

    /* SHT30 单次测量命令 0x2C06 (高重复性) */
    uint8_t cmd[2] = {0x2C, 0x06};
    uint8_t buf[6] = {0};

    if (HAL_I2C_Master_Transmit(&SHT30_I2C, SHT30_I2C_ADDR << 1, cmd, 2, 100) != HAL_OK) {
        return data;
    }
    HAL_Delay(20); /* 等待测量完成 */
    if (HAL_I2C_Master_Receive(&SHT30_I2C, SHT30_I2C_ADDR << 1, buf, 6, 100) != HAL_OK) {
        return data;
    }

    uint16_t t_raw = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t h_raw = ((uint16_t)buf[3] << 8) | buf[4];

    data.temperature = -45.0f + 175.0f * (float)t_raw / 65535.0f;
    data.humidity = 100.0f * (float)h_raw / 65535.0f;
    data.source = TEMP_SRC_SHT30;
    data.timestamp_ms = HAL_GetTick();
    return data;
}
```

- [ ] **Step 3: 编译验证** — 将 temp_adc.c 和 temp_sensor.c 加入工程。

---

### Task 6: RTC 驱动封装

**Files:**
- Create: `firmware/Drivers/BSP/rtc/rtc_drv.c`

- [ ] **Step 1: 创建 rtc_drv.c**

```c
/**
 * rtc_drv.c — RTC 驱动封装实现
 */

#include "rtc_drv.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

static rtc_format_t current_format = RTC_FORMAT_24H;
static bool rtc_valid = false;

/* 中文星期名称 */
static const char *weekday_names[] = {
    "周一", "周二", "周三", "周四", "周五", "周六", "周日"
};

void rtc_drv_init(void) {
    /* 检查 RTC 是否已配置 (备份寄存器非默认值) */
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) == 0x55AA) {
        rtc_valid = true;
    } else {
        rtc_valid = false;
        /* 标记已初始化 */
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0x55AA);
    }

    /* 读取保存的 12/24h 格式 */
    uint32_t fmt = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
    current_format = (fmt == 12) ? RTC_FORMAT_12H : RTC_FORMAT_24H;

    LOG("RTC: %s, format: %dh\r\n", rtc_valid ? "valid" : "first boot", current_format == RTC_FORMAT_12H ? 12 : 24);
}

rtc_datetime_t rtc_drv_get_datetime(void) {
    rtc_datetime_t dt = {0};
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    dt.time.hours   = sTime.Hours;
    dt.time.minutes = sTime.Minutes;
    dt.time.seconds = sTime.Seconds;
    dt.date.day     = sDate.Date;
    dt.date.month   = sDate.Month;
    dt.date.year    = sDate.Year + 2000;
    dt.date.weekday = sDate.WeekDay; /* 1=Monday in HAL */

    return dt;
}

void rtc_drv_set_datetime(const rtc_datetime_t *dt) {
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    sTime.Hours   = dt->time.hours;
    sTime.Minutes = dt->time.minutes;
    sTime.Seconds = dt->time.seconds;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_SET;

    sDate.Date    = dt->date.day;
    sDate.Month   = dt->date.month;
    sDate.Year    = (uint8_t)(dt->date.year - 2000);
    sDate.WeekDay = dt->date.weekday;

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    rtc_valid = true;
}

uint8_t rtc_drv_get_hour12(void) {
    rtc_datetime_t dt = rtc_drv_get_datetime();
    uint8_t h = dt.time.hours;
    if (h == 0) return 12;
    if (h > 12) return h - 12;
    return h;
}

bool rtc_drv_is_pm(void) {
    rtc_datetime_t dt = rtc_drv_get_datetime();
    return dt.time.hours >= 12;
}

void rtc_drv_set_format(rtc_format_t fmt) {
    current_format = fmt;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, fmt == RTC_FORMAT_12H ? 12 : 24);
}

rtc_format_t rtc_drv_get_format(void) {
    return current_format;
}

bool rtc_drv_is_valid(void) {
    return rtc_valid;
}

const char* rtc_drv_weekday_str(uint8_t weekday) {
    if (weekday < 1 || weekday > 7) return "???";
    return weekday_names[weekday - 1];
}
```

- [ ] **Step 2: 编译验证** — 将 rtc_drv.c 加入工程。BSP 层全部完成。

---

## Phase 2: 中间件层（依赖 BSP 驱动）

### Task 7: 轻量 GUI 库

**Files:**
- Create: `firmware/Middleware/gui/gui.c`

- [ ] **Step 1: 创建 gui.c — 脏矩形追踪 + 高级图形**

```c
/**
 * gui.c — 轻量 GUI 库实现
 *
 * 提供脏矩形追踪、弧形进度条、刻度线、数字仪表、图标等高级绘图。
 * 所有绘图函数内部自动调用 st7735 并标记脏矩形。
 */

#include "gui.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- 脏矩形池 ---- */
static dirty_rect_t dirty_rects[MAX_DIRTY_RECTS];
static uint8_t dirty_count = 0;

void gui_init(void) {
    gui_dirty_clear();
    LOG("GUI initialized\r\n");
}

void gui_dirty_mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    /* 裁剪到屏幕范围 */
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0 || h == 0) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    /* 尝试合并到已有脏矩形 */
    for (uint8_t i = 0; i < dirty_count; i++) {
        if (!dirty_rects[i].valid) continue;
        /* 检查重叠或相邻 */
        if (x <= dirty_rects[i].x + dirty_rects[i].w &&
            dirty_rects[i].x <= x + w &&
            y <= dirty_rects[i].y + dirty_rects[i].h &&
            dirty_rects[i].y <= y + h) {
            /* 合并 */
            uint16_t nx = (x < dirty_rects[i].x) ? x : dirty_rects[i].x;
            uint16_t ny = (y < dirty_rects[i].y) ? y : dirty_rects[i].y;
            uint16_t nr = (x + w > dirty_rects[i].x + dirty_rects[i].w) ?
                          (x + w) : (dirty_rects[i].x + dirty_rects[i].w);
            uint16_t nb = (y + h > dirty_rects[i].y + dirty_rects[i].h) ?
                          (y + h) : (dirty_rects[i].y + dirty_rects[i].h);
            dirty_rects[i].x = nx;
            dirty_rects[i].y = ny;
            dirty_rects[i].w = nr - nx;
            dirty_rects[i].h = nb - ny;
            return;
        }
    }

    /* 新增 */
    if (dirty_count < MAX_DIRTY_RECTS) {
        dirty_rects[dirty_count].x = x;
        dirty_rects[dirty_count].y = y;
        dirty_rects[dirty_count].w = w;
        dirty_rects[dirty_count].h = h;
        dirty_rects[dirty_count].valid = true;
        dirty_count++;
    } else {
        /* 槽位满了，合并到第一个 */
        dirty_rects[0].x = 0;
        dirty_rects[0].y = 0;
        dirty_rects[0].w = LCD_WIDTH;
        dirty_rects[0].h = LCD_HEIGHT;
        dirty_rects[0].valid = true;
        dirty_count = 1;
    }
}

void gui_dirty_clear(void) {
    memset(dirty_rects, 0, sizeof(dirty_rects));
    dirty_count = 0;
}

uint8_t gui_dirty_get_count(void) {
    return dirty_count;
}

const dirty_rect_t* gui_dirty_get_all(void) {
    return dirty_rects;
}

dirty_rect_t gui_dirty_merge(void) {
    dirty_rect_t merged = {0, 0, 0, 0, false};
    if (dirty_count == 0) return merged;

    merged.x = LCD_WIDTH;
    merged.y = LCD_HEIGHT;
    uint16_t right = 0, bottom = 0;

    for (uint8_t i = 0; i < dirty_count; i++) {
        if (!dirty_rects[i].valid) continue;
        if (dirty_rects[i].x < merged.x) merged.x = dirty_rects[i].x;
        if (dirty_rects[i].y < merged.y) merged.y = dirty_rects[i].y;
        uint16_t r = dirty_rects[i].x + dirty_rects[i].w;
        uint16_t b = dirty_rects[i].y + dirty_rects[i].h;
        if (r > right) right = r;
        if (b > bottom) bottom = b;
    }

    merged.w = right - merged.x;
    merged.h = bottom - merged.y;
    merged.valid = (merged.w > 0 && merged.h > 0);
    return merged;
}

/* ---- 高级图形 ---- */

void gui_draw_arc(uint16_t cx, uint16_t cy, uint16_t r,
                  int16_t start_deg, int16_t end_deg,
                  uint16_t thickness, uint16_t color) {
    /* 角度归一化 */
    while (start_deg < 0) start_deg += 360;
    while (end_deg < 0) end_deg += 360;
    while (start_deg >= 360) start_deg -= 360;
    while (end_deg >= 360) end_deg -= 360;

    if (end_deg <= start_deg) end_deg += 360;

    /* 逐层画粗弧线 */
    for (uint16_t t = 0; t < thickness; t++) {
        int16_t cr = r - t;
        for (int16_t deg = start_deg; deg <= end_deg; deg += 2) {
            float rad = (float)deg * 3.14159265f / 180.0f;
            uint16_t x = cx + (uint16_t)((float)cr * cosf(rad));
            uint16_t y = cy + (uint16_t)((float)cr * sinf(rad));
            st7735_draw_pixel(x, y, color);
        }
    }
    gui_dirty_mark(cx - r, cy - r, 2 * r, 2 * r);
}

void gui_draw_tick_marks(uint16_t cx, uint16_t cy, uint16_t r,
                         int16_t start_deg, int16_t end_deg,
                         uint8_t count, uint16_t color) {
    if (count < 2) return;
    int16_t range = end_deg - start_deg;
    for (uint8_t i = 0; i < count; i++) {
        float deg = (float)start_deg + (float)range * i / (count - 1);
        float rad = deg * 3.14159265f / 180.0f;
        uint16_t x1 = cx + (uint16_t)((float)(r - 5) * cosf(rad));
        uint16_t y1 = cy + (uint16_t)((float)(r - 5) * sinf(rad));
        uint16_t x2 = cx + (uint16_t)((float)(r - 12) * cosf(rad));
        uint16_t y2 = cy + (uint16_t)((float)(r - 12) * sinf(rad));
        st7735_draw_line(x1, y1, x2, y2, color);
    }
    gui_dirty_mark(cx - r, cy - r, 2 * r, 2 * r);
}

void gui_draw_meter(uint16_t cx, uint16_t cy,
                    const char *value, const char *unit,
                    uint16_t color_val, uint16_t color_unit) {
    /* 大号居中显示数值，下方小字单位 */
    uint16_t vw, vh, uw, uh;
    gui_text_size(value, 3, &vw, &vh);   /* FONT_12x24 */
    gui_text_size(unit, 1, &uw, &uh);    /* FONT_6x8 */

    /* 背景填充区域 */
    uint16_t total_w = vw > uw ? vw : uw;
    uint16_t total_h = vh + uh + 4;
    uint16_t bx = cx - total_w / 2;
    uint16_t by = cy - total_h / 2;
    st7735_fill_rect(bx - 4, by - 4, total_w + 8, total_h + 8, COLOR_BLACK);

    /* 数值 */
    st7735_draw_text(cx - vw / 2, cy - vh / 2, value, FONT_12x24, color_val, COLOR_BLACK);
    /* 单位 */
    st7735_draw_text(cx - uw / 2, cy + vh / 2 + 4, unit, FONT_6x8, color_unit, COLOR_BLACK);

    gui_dirty_mark(bx - 4, by - 4, total_w + 8, total_h + 8);
}

/* 预设图标 (6x6 或 8x8 简单位图) */
static const uint8_t icon_bits[8][8] = {
    /* ICON_SUN: 中心圆 + 8条射线 */
    {0x00,0x08,0x2A,0x1C,0x1C,0x2A,0x08,0x00},
    /* ICON_MOON */
    {0x0E,0x11,0x11,0x0E,0x04,0x0A,0x11,0x00},
    /* ICON_ARROW_UP */
    {0x08,0x1C,0x2A,0x08,0x08,0x08,0x08,0x00},
    /* ICON_ARROW_DOWN */
    {0x08,0x08,0x08,0x08,0x2A,0x1C,0x08,0x00},
    /* ICON_ARROW_LEFT */
    {0x08,0x0C,0x0E,0x1F,0x0E,0x0C,0x08,0x00},
    /* ICON_ARROW_RIGHT */
    {0x04,0x0C,0x1C,0x3E,0x1C,0x0C,0x04,0x00},
    /* ICON_CHECK */
    {0x00,0x01,0x03,0x16,0x1C,0x08,0x00,0x00},
    /* ICON_CROSS */
    {0x00,0x22,0x14,0x08,0x14,0x22,0x00,0x00},
};

void gui_draw_icon(uint16_t x, uint16_t y, gui_icon_t icon, uint16_t color) {
    if (icon >= 8) return;
    for (uint8_t row = 0; row < 8; row++) {
        for (uint8_t col = 0; col < 8; col++) {
            if (icon_bits[icon][row] & (0x80 >> col)) {
                st7735_draw_pixel(x + col, y + row, color);
            }
        }
    }
    gui_dirty_mark(x, y, 8, 8);
}

/* ---- 文本布局 ---- */

void gui_text_size(const char *str, uint8_t font_id, uint16_t *w, uint16_t *h) {
    uint8_t cw, ch;
    switch (font_id) {
        case 0: cw = 6; ch = 8;  break;
        case 1: cw = 8; ch = 16; break;
        case 2: cw = 12; ch = 24; break;
        case 3: cw = 16; ch = 32; break;
        default: cw = 6; ch = 8; break;
    }
    *w = cw * (uint16_t)strlen(str);
    *h = ch;
}

void gui_draw_text_centered(uint16_t cx, uint16_t cy,
                            const char *str, uint8_t font_id,
                            uint16_t fg, uint16_t bg) {
    uint16_t tw, th;
    gui_text_size(str, font_id, &tw, &th);
    uint16_t x = cx - tw / 2;
    uint16_t y = cy - th / 2;
    st7735_draw_text(x, y, str, (font_size_t)font_id, fg, bg);
    gui_dirty_mark(x, y, tw, th);
}

void gui_draw_text_aligned(uint16_t y, const char *str, uint8_t font_id,
                           uint16_t fg, uint16_t bg, gui_align_t align) {
    uint16_t tw, th;
    gui_text_size(str, font_id, &tw, &th);
    uint16_t x;
    switch (align) {
        case GUI_ALIGN_LEFT:   x = 2; break;
        case GUI_ALIGN_CENTER: x = (LCD_WIDTH - tw) / 2; break;
        case GUI_ALIGN_RIGHT:  x = LCD_WIDTH - tw - 2; break;
        default: x = 0; break;
    }
    st7735_draw_text(x, y, str, (font_size_t)font_id, fg, bg);
    gui_dirty_mark(x, y, tw, th);
}
```

- [ ] **Step 2: 编译验证** — 将 gui.c 加入工程，需 `-lm` 链接数学库。

---

### Task 8: 指针引擎

**Files:**
- Create: `firmware/Middleware/pointer/pointer_engine.c`

- [ ] **Step 1: 创建 pointer_engine.c**

```c
/**
 * pointer_engine.c — 指针引擎实现
 *
 * 管理物理指针角度，提供平滑插值和不同刻度映射。
 * 50ms 周期调用 pointer_engine_update()。
 */

#include "pointer_engine.h"
#include "pin_config.h"
#include "app_config.h"
#include <math.h>

static pointer_state_t pstate = {0};

/* ---- 刻度映射: 输入值 → 角度 ---- */

static float scale_clock(uint8_t hour, uint8_t minute) {
    /* 12h 时钟: 每小时30°, 每分钟0.5° */
    return (float)(hour % 12) * 30.0f + (float)minute * 0.5f;
}

static float scale_clock_24h(uint8_t hour, uint8_t minute) {
    /* 24h: 每小时15°, 每分钟0.25° */
    return (float)(hour % 24) * 15.0f + (float)minute * 0.25f;
}

static float scale_temperature(float temp_c, bool fahrenheit) {
    float temp = fahrenheit ? (temp_c * 9.0f / 5.0f + 32.0f) : temp_c;
    float min_val = fahrenheit ? 14.0f : -10.0f;
    float max_val = fahrenheit ? 122.0f : 50.0f;

    if (temp < min_val) temp = min_val;
    if (temp > max_val) temp = max_val;

    /* 300° 弧 (从 30° 到 330°) */
    float ratio = (temp - min_val) / (max_val - min_val);
    return 30.0f + ratio * 300.0f;
}

static float scale_timer(uint32_t remaining_sec, uint32_t total_sec) {
    if (total_sec == 0) return 360.0f;
    float ratio = (float)remaining_sec / (float)total_sec;
    return ratio * 360.0f;
}

static float scale_page(uint8_t page, uint8_t total_pages) {
    if (total_pages <= 1) return 0.0f;
    /* 240° 弧分 N 等分 */
    return 60.0f + (float)page * 240.0f / (float)(total_pages - 1);
}

/* ---- 平滑插值 ---- */

static float lerp_angle(float current, float target, float factor) {
    /* 取最短路径 */
    float diff = target - current;
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    return current + diff * factor;
}

/* ---- API 实现 ---- */

void pointer_engine_init(void) {
    memset(&pstate, 0, sizeof(pstate));
    pstate.current_angle = 0.0f;
    pstate.target_angle = 0.0f;
    pstate.move_mode = POINTER_MOVE_SMOOTH;
    pstate.scale = SCALE_CLOCK;
    pstate.is_moving = false;
    pstate.last_update_ms = HAL_GetTick();
    LOG("Pointer engine initialized\r\n");
}

void pointer_set_target(float angle, pointer_move_mode_t mode) {
    while (angle >= 360.0f) angle -= 360.0f;
    while (angle < 0.0f) angle += 360.0f;
    pstate.target_angle = angle;
    pstate.move_mode = mode;
    pstate.is_moving = true;
}

/* 便捷映射函数 */
void pointer_set_clock(uint8_t hour, uint8_t minute) {
    pointer_set_target(scale_clock(hour, minute), POINTER_MOVE_SMOOTH);
    pstate.scale = SCALE_CLOCK;
}

void pointer_set_clock_24h(uint8_t hour, uint8_t minute) {
    pointer_set_target(scale_clock_24h(hour, minute), POINTER_MOVE_SMOOTH);
    pstate.scale = SCALE_CLOCK;
}

void pointer_set_temperature(float temp_c, bool fahrenheit) {
    pointer_scale_t s = fahrenheit ? SCALE_TEMP_F : SCALE_TEMP_C;
    pointer_set_target(scale_temperature(temp_c, fahrenheit), POINTER_MOVE_NORMAL);
    pstate.scale = s;
}

void pointer_set_timer(uint32_t remaining_sec, uint32_t total_sec) {
    pointer_set_target(scale_timer(remaining_sec, total_sec), POINTER_MOVE_NORMAL);
    pstate.scale = SCALE_TIMER_60;
}

void pointer_set_page(uint8_t page, uint8_t total_pages) {
    pointer_set_target(scale_page(page, total_pages), POINTER_MOVE_FAST);
    pstate.scale = SCALE_PAGE;
}

void pointer_engine_update(void) {
    uint32_t now = HAL_GetTick();
    if (now - pstate.last_update_ms < POINTER_UPDATE_MS) return;
    pstate.last_update_ms = now;

    if (!pstate.is_moving) return;

    /* 根据运动模式选择插值因子 */
    float factor;
    switch (pstate.move_mode) {
        case POINTER_MOVE_SMOOTH: factor = 1.0f / POINTER_SMOOTH_STEPS; break; /* 1秒 */
        case POINTER_MOVE_NORMAL: factor = 1.0f / 10.0f;   break; /* 0.5秒 */
        case POINTER_MOVE_FAST:   factor = 1.0f / 4.0f;    break; /* 0.2秒 */
        case POINTER_MOVE_URGENT: factor = 1.0f;            break; /* 立即跳转 */
        default: factor = 1.0f / POINTER_SMOOTH_STEPS; break;
    }

    pstate.current_angle = lerp_angle(pstate.current_angle, pstate.target_angle, factor);

    /* 到达目标判定 (误差 < 0.1°) */
    float diff = pstate.target_angle - pstate.current_angle;
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    if (fabsf(diff) < 0.1f) {
        pstate.current_angle = pstate.target_angle;
        pstate.is_moving = false;
    }
}

pointer_state_t pointer_get_state(void) {
    return pstate;
}

bool pointer_has_reached_target(void) {
    return !pstate.is_moving;
}

float pointer_get_current_angle(void) {
    return pstate.current_angle;
}

void pointer_calibrate(void) {
    /* 归零校准: 缓慢旋转到机械限位 (由硬件限位开关实现，此处仅归零角度) */
    pstate.current_angle = 0.0f;
    pstate.target_angle = 0.0f;
    pstate.is_moving = false;
    LOG("Pointer calibrated to 0°\r\n");
}
```

- [ ] **Step 2: 编译验证** — 将 pointer_engine.c 加入工程。

---

### Task 9: LittleFS 文件系统管理

**Files:**
- Create: `firmware/Middleware/fs/fs_mgr.c`

- [ ] **Step 1: 创建 fs_mgr.c**

```c
/**
 * fs_mgr.c — LittleFS 文件系统管理实现
 *
 * W25Q64 上分两个区：配置区 (1MB, R/W) + 资源区 (7MB, R/O)
 * 使用 LittleFS 的磨损均衡和掉电保护。
 */

#include "fs_mgr.h"
#include "w25q64.h"
#include "pin_config.h"
#include "app_config.h"
#include <string.h>
#include "lfs.h"

/* ---- LittleFS 实例 ---- */
static lfs_t lfs_cfg;          /* 配置区 LittleFS */
static lfs_t lfs_res;          /* 资源区 LittleFS */

/* ---- 配置区存储块定义 ---- */
static struct lfs_config cfg_lfs_config;
static struct lfs_config res_lfs_config;

static bool cfg_mounted = false;
static bool res_mounted = false;

/* ---- LittleFS 底层块设备适配 ---- */

static int flash_read(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t addr = (uint32_t)(uintptr_t)c->context + (uint32_t)(block * c->block_size) + off;
    w25q64_read(addr, (uint8_t *)buffer, size);
    return 0;
}

static int flash_prog(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t addr = (uint32_t)(uintptr_t)c->context + (uint32_t)(block * c->block_size) + off;
    w25q64_write(addr, (const uint8_t *)buffer, size);
    return 0;
}

static int flash_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t addr = (uint32_t)(uintptr_t)c->context + (uint32_t)(block * c->block_size);
    /* 使用 4KB 扇区擦除 (W25Q64 最小擦除单位) */
    w25q64_erase_sector(addr);
    return 0;
}

static int flash_sync(const struct lfs_config *c) {
    (void)c;
    return 0;
}

/* ---- API 实现 ---- */

void fs_mgr_init(void) {
    /* 初始化 W25Q64 */
    w25q64_init();

    /* --- 配置区 LittleFS --- */
    cfg_lfs_config.read       = flash_read;
    cfg_lfs_config.prog       = flash_prog;
    cfg_lfs_config.erase      = flash_erase;
    cfg_lfs_config.sync       = flash_sync;
    cfg_lfs_config.read_size   = LFS_READ_SIZE;
    cfg_lfs_config.prog_size   = LFS_PROG_SIZE;
    cfg_lfs_config.block_size  = LFS_BLOCK_SIZE;
    cfg_lfs_config.block_count = LFS_CONFIG_SIZE / LFS_BLOCK_SIZE;
    cfg_lfs_config.cache_size  = LFS_CACHE_SIZE;
    cfg_lfs_config.lookahead_size = LFS_LOOKAHEAD_SIZE;
    cfg_lfs_config.block_cycles = 500;
    cfg_lfs_config.context     = (void *)(uintptr_t)LFS_CONFIG_OFFSET;

    int err = lfs_mount(&lfs_cfg, &cfg_lfs_config);
    if (err) {
        LOG("Formatting config LittleFS...\r\n");
        lfs_format(&lfs_cfg, &cfg_lfs_config);
        err = lfs_mount(&lfs_cfg, &cfg_lfs_config);
    }
    cfg_mounted = (err == 0);
    if (cfg_mounted) {
        LOG("Config LittleFS mounted (%lu KB)\r\n", LFS_CONFIG_SIZE / 1024);
    }

    /* --- 资源区 LittleFS (只读挂载) --- */
    res_lfs_config = cfg_lfs_config;
    res_lfs_config.block_count = LFS_FONT_SIZE / LFS_BLOCK_SIZE;
    res_lfs_config.context     = (void *)(uintptr_t)LFS_FONT_OFFSET;

    err = lfs_mount(&lfs_res, &res_lfs_config);
    res_mounted = (err == 0);
}

bool fs_mgr_is_mounted(void) {
    return cfg_mounted;
}

/* ---- 配置区 Key-Value 存储 ---- */

bool fs_config_save(const char *key, const void *data, size_t size) {
    if (!cfg_mounted) return false;

    lfs_file_t file;
    char path[64];
    snprintf(path, sizeof(path), "/cfg/%s", key);

    /* 确保目录存在 */
    lfs_mkdir(&lfs_cfg, "/cfg");

    int err = lfs_file_open(&lfs_cfg, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err) return false;

    lfs_file_write(&lfs_cfg, &file, data, size);
    lfs_file_close(&lfs_cfg, &file);
    return true;
}

bool fs_config_load(const char *key, void *data, size_t size) {
    if (!cfg_mounted) return false;

    lfs_file_t file;
    char path[64];
    snprintf(path, sizeof(path), "/cfg/%s", key);

    int err = lfs_file_open(&lfs_cfg, &file, path, LFS_O_RDONLY);
    if (err) return false;

    lfs_ssize_t read = lfs_file_read(&lfs_cfg, &file, data, size);
    lfs_file_close(&lfs_cfg, &file);
    return (read > 0);
}

bool fs_config_exists(const char *key) {
    if (!cfg_mounted) return false;

    char path[64];
    snprintf(path, sizeof(path), "/cfg/%s", key);

    struct lfs_info info;
    return (lfs_stat(&lfs_cfg, path, &info) == 0);
}

bool fs_config_delete(const char *key) {
    if (!cfg_mounted) return false;

    char path[64];
    snprintf(path, sizeof(path), "/cfg/%s", key);
    return (lfs_remove(&lfs_cfg, path) == 0);
}

void fs_config_format(void) {
    if (!cfg_mounted) return;

    lfs_file_t file;
    /* 遍历并删除 /cfg/ 目录下所有文件 */
    lfs_dir_t dir;
    struct lfs_info info;
    lfs_dir_open(&lfs_cfg, &dir, "/cfg");
    while (lfs_dir_read(&lfs_cfg, &dir, &info) > 0) {
        if (info.type == LFS_TYPE_REG) {
            char path[64];
            snprintf(path, sizeof(path), "/cfg/%s", info.name);
            lfs_remove(&lfs_cfg, path);
        }
    }
    lfs_dir_close(&lfs_cfg, &dir);
    LOG("Config partition formatted\r\n");
}

/* ---- 资源区 ---- */

void* fs_res_open(const char *path) {
    /* 资源区文件内容全部读入内存 (适合小图标/字体) */
    if (!res_mounted) return NULL;

    lfs_file_t file;
    int err = lfs_file_open(&lfs_res, &file, path, LFS_O_RDONLY);
    if (err) return NULL;

    lfs_soff_t size = lfs_file_size(&lfs_res, &file);
    if (size <= 0) { lfs_file_close(&lfs_res, &file); return NULL; }

    void *buf = pvPortMalloc((size_t)size); /* FreeRTOS 堆分配 */
    if (!buf) { lfs_file_close(&lfs_res, &file); return NULL; }

    lfs_file_read(&lfs_res, &file, buf, (lfs_size_t)size);
    lfs_file_close(&lfs_res, &file);
    return buf;
}

size_t fs_res_size(const char *path) {
    if (!res_mounted) return 0;
    struct lfs_info info;
    if (lfs_stat(&lfs_res, path, &info) != 0) return 0;
    return (size_t)info.size;
}

bool fs_res_exists(const char *path) {
    if (!res_mounted) return false;
    struct lfs_info info;
    return (lfs_stat(&lfs_res, path, &info) == 0);
}

/* ---- 通用文件操作 ---- */

bool fs_file_read(const char *path, void *buf, size_t size) {
    if (!cfg_mounted) return false;
    lfs_file_t file;
    if (lfs_file_open(&lfs_cfg, &file, path, LFS_O_RDONLY) != 0) return false;
    lfs_ssize_t r = lfs_file_read(&lfs_cfg, &file, buf, size);
    lfs_file_close(&lfs_cfg, &file);
    return (r > 0);
}

bool fs_file_write(const char *path, const void *buf, size_t size) {
    if (!cfg_mounted) return false;
    lfs_file_t file;
    if (lfs_file_open(&lfs_cfg, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0) return false;
    lfs_ssize_t w = lfs_file_write(&lfs_cfg, &file, buf, size);
    lfs_file_close(&lfs_cfg, &file);
    return (w > 0);
}

bool fs_file_remove(const char *path) {
    if (!cfg_mounted) return false;
    return (lfs_remove(&lfs_cfg, path) == 0);
}

uint32_t fs_get_free_config(void) {
    if (!cfg_mounted) return 0;
    /* 遍历计数 */
    lfs_ssize_t used = lfs_fs_size(&lfs_cfg);
    if (used < 0) return 0;
    return LFS_CONFIG_SIZE - (uint32_t)used * LFS_BLOCK_SIZE;
}

uint32_t fs_get_total_config(void) {
    return LFS_CONFIG_SIZE;
}
```

> **注意:** 此任务依赖 LittleFS 源码存在于 `Middlewares/Third_Party/LittleFS/` 中。若尚未添加，需提前将 LittleFS 库加入工程路径。

- [ ] **Step 2: 编译验证** — 将 fs_mgr.c 加入工程，中间件层全部完成。

---

## Phase 3: 应用层 — 四种模式 + 状态机

### Task 10: 时钟模式

**Files:**
- Create: `firmware/App/modes/clock_mode.c`

- [ ] **Step 1: 创建 clock_mode.c**

```c
/**
 * clock_mode.c — 时钟模式实现
 *
 * TFT 显示: 时间 HH:MM:SS、日期、星期、AM/PM 图标、24h 时间条
 * 指针: 12h/24h 模拟时钟
 */

#include "clock_mode.h"
#include "rtc_drv.h"
#include "gui.h"
#include "pointer_engine.h"
#include "pin_config.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

static bool use_24h = false;

void clock_mode_init(void) {
    use_24h = (rtc_drv_get_format() == RTC_FORMAT_24H);
}

void clock_mode_enter(void) {
    LOG("Clock mode entered\r\n");
    /* 全屏刷新 */
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void clock_mode_exit(void) {
    /* 无清理操作 */
}

void clock_mode_update(void) {
    rtc_datetime_t dt = rtc_drv_get_datetime();

    /* 更新指针 */
    if (use_24h) {
        pointer_set_clock_24h(dt.time.hours, dt.time.minutes);
    } else {
        pointer_set_clock(dt.time.hours, dt.time.minutes);
    }
}

void clock_mode_render(void) {
    rtc_datetime_t dt = rtc_drv_get_datetime();

    /* ---- 顶行: AM/PM 图标 ---- */
    if (!use_24h) {
        gui_icon_t ampm = rtc_drv_is_pm() ? ICON_MOON : ICON_SUN;
        gui_draw_icon(LCD_WIDTH - 14, 4, ampm, COLOR_YELLOW);
    } else {
        /* 24H 标记 */
        st7735_fill_rect(LCD_WIDTH - 14, 4, 12, 8, COLOR_BLACK);
    }

    /* ---- 中间: 时间 HH:MM:SS (大字体) ---- */
    char time_str[16];
    if (use_24h) {
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
                 dt.time.hours, dt.time.minutes, dt.time.seconds);
    } else {
        uint8_t h12 = rtc_drv_get_hour12();
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
                 h12, dt.time.minutes, dt.time.seconds);
    }
    gui_draw_text_centered(LCD_WIDTH / 2, 40, time_str, 2, COLOR_WHITE, COLOR_BLACK);

    /* ---- 日期行 ---- */
    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d  %s",
             (int)dt.date.year, dt.date.month, dt.date.day,
             rtc_drv_weekday_str(dt.date.weekday));
    gui_draw_text_centered(LCD_WIDTH / 2, 70, date_str, 0, COLOR_GRAY, COLOR_BLACK);

    /* ---- 底部: 24 小时浓缩时间条 ---- */
    uint16_t bar_y = 140;
    uint16_t bar_x = 5;
    uint16_t bar_w = LCD_WIDTH - 10;
    uint16_t bar_h = 4;

    /* 时间条背景 */
    st7735_fill_rect(bar_x, bar_y, bar_w, bar_h, COLOR_DARK_GRAY);
    /* 已过时间的白点 */
    uint16_t dot_pos = (uint16_t)((uint32_t)bar_w * dt.time.hours * 60 / 1440);
    st7735_fill_rect(bar_x + dot_pos, bar_y, 3, bar_h, COLOR_WHITE);
    gui_dirty_mark(bar_x, bar_y, bar_w, bar_h);
}

void clock_mode_handle_button(button_id_t btn, button_event_t event) {
    if (event == BTN_EVENT_SHORT_PRESS) {
        if (btn == BTN_UP || btn == BTN_DOWN) {
            /* 切换 12/24h */
            use_24h = !use_24h;
            rtc_drv_set_format(use_24h ? RTC_FORMAT_24H : RTC_FORMAT_12H);
            /* 触发全屏刷新 */
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
    }
}

bool clock_mode_use_24h(void) {
    return use_24h;
}
```

- [ ] **Step 2: 编译验证** — 将 clock_mode.c 加入工程。

---

### Task 11: 温度计模式

**Files:**
- Create: `firmware/App/modes/temp_mode.c`

- [ ] **Step 1: 创建 temp_mode.c**

```c
/**
 * temp_mode.c — 温度计模式实现
 */

#include "temp_mode.h"
#include "temp_sensor.h"
#include "gui.h"
#include "pointer_engine.h"
#include "pin_config.h"
#include "app_config.h"
#include <stdio.h>

static bool fahrenheit = false;
static float current_temp = 0.0f;
static uint32_t last_read_ms = 0;

void temp_mode_init(void) {
    fahrenheit = false;
}

void temp_mode_enter(void) {
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void temp_mode_exit(void) {}

void temp_mode_update(void) {
    uint32_t now = HAL_GetTick();
    if (now - last_read_ms < TEMP_READ_INTERVAL_MS) return;
    last_read_ms = now;

    temp_data_t data = temp_sensor_read();
    current_temp = data.temperature;

    /* 更新指针 */
    pointer_set_temperature(current_temp, fahrenheit);
}

void temp_mode_render(void) {
    float display_temp = fahrenheit ?
        (current_temp * 9.0f / 5.0f + 32.0f) : current_temp;

    /* ---- 弧形刻度盘 (中心稍靠上) ---- */
    uint16_t cx = LCD_WIDTH / 2;
    uint16_t cy = 70;
    uint16_t r = 45;

    /* 画刻度弧线背景 */
    gui_draw_arc(cx, cy, r, 30, 330, 3, COLOR_DARK_GRAY);
    /* 画 12 条刻度线 */
    gui_draw_tick_marks(cx, cy, r, 30, 330, 12, COLOR_GRAY);

    /* ---- 中心温度数值 ---- */
    char val_str[16], unit_str[4];
    snprintf(val_str, sizeof(val_str), "%.1f", display_temp);
    snprintf(unit_str, sizeof(unit_str), "%s", fahrenheit ? "°F" : "°C");
    gui_draw_meter(LCD_WIDTH / 2, cy + 5, val_str, unit_str, COLOR_WHITE, COLOR_CYAN);

    /* ---- 状态行 ---- */
    /* 传感器标签 */
    char label[32];
    snprintf(label, sizeof(label), "%s", temp_sensor_get_label());
    gui_draw_text_aligned(140, label, 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);

    /* ---- 箭头指示趋势 ---- */
    /* (简化版: 先不实现趋势判断，仅显示静态指示) */
}

void temp_mode_handle_button(button_id_t btn, button_event_t event) {
    if (event == BTN_EVENT_SHORT_PRESS) {
        if (btn == BTN_UP || btn == BTN_DOWN) {
            /* ℃/℉ 切换 */
            fahrenheit = !fahrenheit;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
    }
}

bool temp_mode_is_fahrenheit(void) {
    return fahrenheit;
}
```

- [ ] **Step 2: 编译验证** — 将 temp_mode.c 加入工程。

---

### Task 12: 计时器模式

**Files:**
- Create: `firmware/App/modes/timer_mode.c`

- [ ] **Step 1: 创建 timer_mode.c**

```c
/**
 * timer_mode.c — 计时器模式实现
 *
 * 倒计时/正计时 + 进度环 + 指针归零 + 结束震动
 * 操作: 上/下键调整时长, 中键开始/暂停, 左键复位
 */

#include "timer_mode.h"
#include "gui.h"
#include "pointer_engine.h"
#include "a4988.h"
#include "pin_config.h"
#include "app_config.h"
#include <stdio.h>

typedef enum {
    TIMER_STOPPED,
    TIMER_RUNNING,
    TIMER_PAUSED,
    TIMER_FINISHED,
} timer_state_t;

static timer_state_t state = TIMER_STOPPED;
static uint32_t total_sec = TIMER_DEFAULT_MINUTES * 60;
static uint32_t remaining_sec = TIMER_DEFAULT_MINUTES * 60;
static uint32_t start_tick = 0;
static uint32_t finish_tick = 0;

void timer_mode_init(void) {
    total_sec = TIMER_DEFAULT_MINUTES * 60;
    remaining_sec = total_sec;
    state = TIMER_STOPPED;
}

void timer_mode_enter(void) {
    if (state == TIMER_STOPPED) {
        remaining_sec = total_sec;
    }
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void timer_mode_exit(void) {
    if (state == TIMER_RUNNING) {
        state = TIMER_PAUSED; /* 切出时自动暂停 */
    }
}

void timer_mode_update(void) {
    if (state == TIMER_RUNNING) {
        uint32_t now = HAL_GetTick();
        uint32_t elapsed = (now - start_tick) / 1000;
        if (elapsed < remaining_sec) {
            remaining_sec = total_sec - elapsed;
        } else {
            remaining_sec = 0;
            state = TIMER_FINISHED;
            finish_tick = now;
            /* 触发震动 */
            a4988_vibrate(TIMER_END_VIBRATE_COUNT, 200);
        }
    }

    if (state == TIMER_FINISHED) {
        uint32_t now = HAL_GetTick();
        if (now - finish_tick > TIMER_END_BUZZ_MS) {
            /* 自动返回时钟模式 (由 mode_manager 处理) */
        }
    }

    /* 最后10秒紧急跳跃 */
    pointer_move_mode_t move = (remaining_sec <= TIMER_URGENT_SECONDS && state == TIMER_RUNNING)
                               ? POINTER_MOVE_URGENT : POINTER_MOVE_NORMAL;
    pointer_set_timer(remaining_sec, total_sec);
}

void timer_mode_render(void) {
    /* ---- 进度环 (屏幕上半部分) ---- */
    uint16_t cx = LCD_WIDTH / 2;
    uint16_t cy = 55;
    uint16_t r = 40;

    /* 灰色环背景 */
    gui_draw_arc(cx, cy, r, 0, 360, 4, COLOR_DARK_GRAY);

    if (total_sec > 0 && remaining_sec > 0) {
        float ratio = 1.0f - (float)remaining_sec / (float)total_sec;
        int16_t end_deg = (int16_t)(ratio * 360.0f + 0.5f);
        if (end_deg < 0) end_deg = 0;
        if (end_deg > 360) end_deg = 360;

        uint16_t arc_color = (remaining_sec <= TIMER_URGENT_SECONDS) ? COLOR_RED : COLOR_GREEN;
        if (end_deg > 0) {
            gui_draw_arc(cx, cy, r, 0, end_deg, 4, arc_color);
        }
    }

    /* ---- 时间数字 ---- */
    uint16_t mins = remaining_sec / 60;
    uint16_t secs = remaining_sec % 60;
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", mins, secs);
    gui_draw_text_centered(LCD_WIDTH / 2, 55, time_str, 3,
                           state == TIMER_FINISHED ? COLOR_RED : COLOR_WHITE, COLOR_BLACK);

    /* ---- 状态标签 ---- */
    const char *status;
    switch (state) {
        case TIMER_STOPPED:  status = "STOP"; break;
        case TIMER_RUNNING:  status = "RUN";  break;
        case TIMER_PAUSED:   status = "PAUSE"; break;
        case TIMER_FINISHED: status = "DONE!"; break;
        default: status = ""; break;
    }
    gui_draw_text_centered(LCD_WIDTH / 2, 100, status, 1,
                           state == TIMER_FINISHED ? COLOR_YELLOW : COLOR_GRAY, COLOR_BLACK);

    /* ---- 操作提示 ---- */
    gui_draw_text_aligned(150, "+/-: SET  OK: START  <: RESET", 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);
}

void timer_mode_handle_button(button_id_t btn, button_event_t event) {
    if (state == TIMER_RUNNING) {
        /* 运行中只能暂停 */
        if (btn == BTN_CENTER && event == BTN_EVENT_SHORT_PRESS) {
            state = TIMER_PAUSED;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
        return;
    }

    if (state == TIMER_FINISHED) {
        if (event == BTN_EVENT_SHORT_PRESS) {
            /* 任意键退出结束状态 */
            remaining_sec = total_sec;
            state = TIMER_STOPPED;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
        return;
    }

    /* STOPPED / PAUSED */
    if (btn == BTN_UP) {
        uint32_t step = (event == BTN_EVENT_LONG_PRESS || event == BTN_EVENT_LONG_REPEAT)
                        ? 600 : 60;
        if (event == BTN_EVENT_SHORT_PRESS || event == BTN_EVENT_LONG_PRESS ||
            event == BTN_EVENT_LONG_REPEAT) {
            total_sec += step;
            if (total_sec > 7200) total_sec = 7200; /* 最大 2h */
            if (state == TIMER_STOPPED) remaining_sec = total_sec;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
    } else if (btn == BTN_DOWN) {
        uint32_t step = (event == BTN_EVENT_LONG_PRESS || event == BTN_EVENT_LONG_REPEAT)
                        ? 600 : 60;
        if (event == BTN_EVENT_SHORT_PRESS || event == BTN_EVENT_LONG_PRESS ||
            event == BTN_EVENT_LONG_REPEAT) {
            if (total_sec > step) total_sec -= step;
            else total_sec = 60;
            if (state == TIMER_STOPPED) remaining_sec = total_sec;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
    } else if (btn == BTN_CENTER && event == BTN_EVENT_SHORT_PRESS) {
        if (state == TIMER_PAUSED) {
            /* 继续 */
            state = TIMER_RUNNING;
            start_tick = HAL_GetTick() - (total_sec - remaining_sec) * 1000;
        } else {
            /* 开始 */
            remaining_sec = total_sec;
            start_tick = HAL_GetTick();
            state = TIMER_RUNNING;
        }
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    } else if (btn == BTN_LEFT && event == BTN_EVENT_SHORT_PRESS) {
        /* 复位 */
        remaining_sec = total_sec;
        state = TIMER_STOPPED;
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    }
}

bool timer_mode_is_running(void) {
    return state == TIMER_RUNNING;
}

uint32_t timer_mode_get_remaining_sec(void) {
    return remaining_sec;
}

uint32_t timer_mode_get_total_sec(void) {
    return total_sec;
}
```

- [ ] **Step 2: 编译验证** — 将 timer_mode.c 加入工程。

---

### Task 13: 设置菜单模式

**Files:**
- Create: `firmware/App/modes/settings_mode.c`

- [ ] **Step 1: 创建 settings_mode.c**

```c
/**
 * settings_mode.c — 设置菜单模式实现
 *
 * 分页结构: 第1页 亮度/日期时间/温度单位, 第2页 计时默认/恢复出厂/关于
 * 指针辅助定位当前页码
 */

#include "settings_mode.h"
#include "gui.h"
#include "pointer_engine.h"
#include "rtc_drv.h"
#include "fs_mgr.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>

#define MAX_PAGES       2
#define ITEMS_PER_PAGE  3

typedef enum {
    ST_MAIN_MENU,
    ST_BRIGHTNESS,
    ST_DATETIME,
    ST_TEMP_UNIT,
    ST_TIMER_DEFAULT,
    ST_FACTORY_RESET,
    ST_ABOUT,
} settings_substate_t;

static settings_substate_t substate;
static uint8_t current_page;
static uint8_t cursor;
static uint8_t brightness; /* 0-10 档 */
static uint8_t brightness_value; /* 编辑中的亮度值 */

/* 菜单项定义 */
static const char *page1_items[ITEMS_PER_PAGE] = {
    "Brightness",
    "Date & Time",
    "Temperature Unit",
};

static const char *page2_items[ITEMS_PER_PAGE] = {
    "Timer Default",
    "Factory Reset",
    "About",
};

void settings_mode_init(void) {
    current_page = 0;
    cursor = 0;
    /* 从 Flash 加载亮度设置 */
    if (!fs_config_load("brightness", &brightness, sizeof(brightness))) {
        brightness = LCD_BRIGHTNESS_DEFAULT / 10; /* 默认 8 档 */
    }
}

void settings_mode_enter(void) {
    substate = ST_MAIN_MENU;
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void settings_mode_exit(void) {
    /* 保存设置 */
    fs_config_save("brightness", &brightness, sizeof(brightness));
}

void settings_mode_update(void) {
    pointer_set_page(current_page, MAX_PAGES);
}

void settings_mode_render(void) {
    if (substate == ST_MAIN_MENU) {
        /* 页码标题 */
        char title[32];
        snprintf(title, sizeof(title), "Settings %d/%d", current_page + 1, MAX_PAGES);
        gui_draw_text_centered(LCD_WIDTH / 2, 8, title, 1, COLOR_WHITE, COLOR_BLACK);

        /* 菜单项列表 */
        const char **items = (current_page == 0) ? page1_items : page2_items;
        for (uint8_t i = 0; i < ITEMS_PER_PAGE; i++) {
            uint16_t y = 40 + i * 30;
            if (i == cursor) {
                /* 反色高亮 */
                st7735_fill_rect(0, y - 2, LCD_WIDTH, 24, COLOR_BLUE);
                gui_draw_text_aligned(y, items[i], 1, COLOR_WHITE, COLOR_BLUE, GUI_ALIGN_CENTER);
            } else {
                gui_draw_text_aligned(y, items[i], 1, COLOR_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);
            }
        }

        /* 底部提示 */
        gui_draw_text_aligned(150, "^v:nav  OK:enter  <:back", 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);

    } else if (substate == ST_BRIGHTNESS) {
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Brightness", 1, COLOR_WHITE, COLOR_BLACK);

        /* 亮度条 */
        uint16_t bar_x = 20, bar_y = 60, bar_w = LCD_WIDTH - 40, bar_h = 10;
        st7735_draw_rect(bar_x, bar_y, bar_w, bar_h, COLOR_GRAY);
        st7735_fill_rect(bar_x + 2, bar_y + 2,
                         (uint16_t)((bar_w - 4) * brightness_value / 10), bar_h - 4, COLOR_CYAN);

        char pct_str[8];
        snprintf(pct_str, sizeof(pct_str), "%d%%", brightness_value * 10);
        gui_draw_text_centered(LCD_WIDTH / 2, 90, pct_str, 2, COLOR_WHITE, COLOR_BLACK);

        gui_draw_text_aligned(150, "^v:adjust  OK:save  <:back", 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);

    } else if (substate == ST_DATETIME) {
        /* 日期时间设置界面 (简化: 显示当前时间 + 提示) */
        rtc_datetime_t dt = rtc_drv_get_datetime();
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 (int)dt.date.year, dt.date.month, dt.date.day,
                 dt.time.hours, dt.time.minutes, dt.time.seconds);
        gui_draw_text_centered(LCD_WIDTH / 2, 50, buf, 1, COLOR_WHITE, COLOR_BLACK);

        /* TODO: 完整日期时间编辑器 (上下键调整数值, OK 切换字段) */
        gui_draw_text_aligned(150, "<: back to menu", 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);

    } else if (substate == ST_TEMP_UNIT) {
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Temperature Unit", 1, COLOR_WHITE, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 70, "°C  <-->  °F", 1, COLOR_YELLOW, COLOR_BLACK);
        gui_draw_text_aligned(150, "^v:switch  OK:save  <:back", 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);

    } else if (substate == ST_TIMER_DEFAULT) {
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Timer Default", 1, COLOR_WHITE, COLOR_BLACK);
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu min", TIMER_DEFAULT_MINUTES);
        gui_draw_text_centered(LCD_WIDTH / 2, 70, buf, 2, COLOR_CYAN, COLOR_BLACK);
        gui_draw_text_aligned(150, "^v:adjust  OK:save  <:back", 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);

    } else if (substate == ST_FACTORY_RESET) {
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Factory Reset?", 1, COLOR_RED, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 60, "This will erase", 0, COLOR_RED, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 75, "all settings!", 0, COLOR_RED, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 105, "OK: confirm  <: cancel", 0, COLOR_YELLOW, COLOR_BLACK);

    } else if (substate == ST_ABOUT) {
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "OV-Watch", 2, COLOR_CYAN, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 65, "v1.0.0", 1, COLOR_WHITE, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 90, "github.com/No-Chicken/ov-watch", 0, COLOR_DARK_GRAY, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 110, "Built: " __DATE__, 0, COLOR_DARK_GRAY, COLOR_BLACK);
        gui_draw_text_aligned(150, "<: back", 0, COLOR_DARK_GRAY, COLOR_BLACK, GUI_ALIGN_CENTER);
    }
}

void settings_mode_handle_button(button_id_t btn, button_event_t event) {
    if (event != BTN_EVENT_SHORT_PRESS) return;

    if (substate == ST_MAIN_MENU) {
        switch (btn) {
            case BTN_UP:
                if (cursor > 0) cursor--;
                break;
            case BTN_DOWN:
                if (cursor < ITEMS_PER_PAGE - 1) cursor++;
                break;
            case BTN_LEFT:
                /* 返回上一页 */
                if (current_page > 0) { current_page--; cursor = 0; }
                break;
            case BTN_RIGHT:
                /* 下一页 */
                if (current_page < MAX_PAGES - 1) { current_page++; cursor = 0; }
                break;
            case BTN_CENTER:
                /* 进入子项 */
                {
                    uint8_t item = current_page * ITEMS_PER_PAGE + cursor;
                    switch (item) {
                        case 0: substate = ST_BRIGHTNESS; brightness_value = brightness; break;
                        case 1: substate = ST_DATETIME; break;
                        case 2: substate = ST_TEMP_UNIT; break;
                        case 3: substate = ST_TIMER_DEFAULT; break;
                        case 4: substate = ST_FACTORY_RESET; break;
                        case 5: substate = ST_ABOUT; break;
                    }
                    st7735_fill_screen(COLOR_BLACK);
                    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
                }
                break;
            default: break;
        }
        /* 标记脏矩形 */
        st7735_fill_rect(0, 38, LCD_WIDTH, LCD_HEIGHT - 38, COLOR_BLACK);
        gui_dirty_mark(0, 38, LCD_WIDTH, LCD_HEIGHT - 38);

    } else if (substate == ST_BRIGHTNESS) {
        switch (btn) {
            case BTN_UP:
                if (brightness_value < 10) brightness_value++;
                st7735_set_brightness(brightness_value * 10);
                break;
            case BTN_DOWN:
                if (brightness_value > 1) brightness_value--;
                st7735_set_brightness(brightness_value * 10);
                break;
            case BTN_CENTER:
                brightness = brightness_value;
                fs_config_save("brightness", &brightness, sizeof(brightness));
                substate = ST_MAIN_MENU;
                break;
            case BTN_LEFT:
                substate = ST_MAIN_MENU;
                break;
            default: break;
        }
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);

    } else if (substate == ST_FACTORY_RESET) {
        if (btn == BTN_CENTER) {
            /* 确认恢复出厂 */
            fs_config_format();
            brightness = LCD_BRIGHTNESS_DEFAULT / 10;
            st7735_set_brightness(brightness * 10);
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        } else if (btn == BTN_LEFT) {
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }

    } else {
        /* 其他子菜单: LEFT 返回 */
        if (btn == BTN_LEFT) {
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
    }
}
```

- [ ] **Step 2: 编译验证** — 将 settings_mode.c 加入工程。

---

### Task 14: 模式状态机

**Files:**
- Create: `firmware/App/mode_manager.c`

- [ ] **Step 1: 创建 mode_manager.c**

```c
/**
 * mode_manager.c — 模式状态机实现
 *
 * 管理 4 种模式切换 + 应用启动状态机 (STARTUP → TIME_SETUP / RUNNING)
 */

#include "mode_manager.h"
#include "clock_mode.h"
#include "temp_mode.h"
#include "timer_mode.h"
#include "settings_mode.h"
#include "rtc_drv.h"
#include "gui.h"
#include "st7735.h"
#include "app_config.h"

static app_state_t app_state = APP_STATE_STARTUP;
static mode_id_t current_mode = MODE_CLOCK;

/* ---- 模式切换表 ---- */
typedef void (*mode_fn_init)(void);
typedef void (*mode_fn_enter)(void);
typedef void (*mode_fn_exit)(void);
typedef void (*mode_fn_update)(void);
typedef void (*mode_fn_render)(void);
typedef void (*mode_fn_button)(button_id_t, button_event_t);

static const struct {
    mode_fn_init   init;
    mode_fn_enter  enter;
    mode_fn_exit   exit;
    mode_fn_update update;
    mode_fn_render render;
    mode_fn_button button;
    const char *name;
} modes[MODE_COUNT] = {
    [MODE_CLOCK]    = {clock_mode_init,    clock_mode_enter,    clock_mode_exit,
                       clock_mode_update,  clock_mode_render,  clock_mode_handle_button,  "Clock"},
    [MODE_TEMP]     = {temp_mode_init,     temp_mode_enter,     temp_mode_exit,
                       temp_mode_update,   temp_mode_render,   temp_mode_handle_button,   "Temp"},
    [MODE_TIMER]    = {timer_mode_init,    timer_mode_enter,    timer_mode_exit,
                       timer_mode_update,  timer_mode_render,  timer_mode_handle_button,  "Timer"},
    [MODE_SETTINGS] = {settings_mode_init, settings_mode_enter, settings_mode_exit,
                       settings_mode_update,settings_mode_render,settings_mode_handle_button, "Settings"},
};

/* ---- API 实现 ---- */

void mode_manager_init(void) {
    /* 初始化所有模式 */
    for (int i = 0; i < MODE_COUNT; i++) {
        if (modes[i].init) modes[i].init();
    }

    /* 检查 RTC 有效性 */
    rtc_drv_init();
    if (!rtc_drv_is_valid()) {
        app_state = APP_STATE_TIME_SETUP;
        LOG("RTC not set, entering time setup\r\n");
    } else {
        app_state = APP_STATE_RUNNING;
    }

    /* 进入默认模式 (时钟) */
    mode_manager_switch_to(MODE_CLOCK);
    LOG("Mode manager initialized, current: %s\r\n", modes[current_mode].name);
}

void mode_manager_switch_to(mode_id_t mode) {
    if (mode == current_mode && app_state == APP_STATE_RUNNING) return;

    /* 退出当前模式 */
    if (modes[current_mode].exit) {
        modes[current_mode].exit();
    }

    /* 进入新模式 */
    current_mode = mode;
    if (modes[current_mode].enter) {
        modes[current_mode].enter();
    }

    LOG("Switched to %s mode\r\n", modes[current_mode].name);
}

mode_id_t mode_manager_get_current(void) {
    return current_mode;
}

void mode_manager_handle_button(button_id_t btn, button_event_t event) {
    if (event == BTN_EVENT_NONE) return;

    if (app_state == APP_STATE_TIME_SETUP) {
        /* 时间设置界面 (简化: 用设置模式替代) */
        mode_manager_set_app_state(APP_STATE_RUNNING);
        mode_manager_switch_to(MODE_SETTINGS);
        return;
    }

    /* 全局按键: 左右切换模式 */
    if (event == BTN_EVENT_SHORT_PRESS) {
        if (btn == BTN_LEFT) {
            /* 前一个模式 */
            mode_id_t next = (mode_id_t)(current_mode == 0 ? MODE_COUNT - 1 : current_mode - 1);
            mode_manager_switch_to(next);
            return;
        } else if (btn == BTN_RIGHT) {
            /* 后一个模式 */
            mode_id_t next = (mode_id_t)((current_mode + 1) % MODE_COUNT);
            mode_manager_switch_to(next);
            return;
        }
    }

    /* 分发到当前模式 */
    if (modes[current_mode].button) {
        modes[current_mode].button(btn, event);
    }
}

void mode_manager_update(void) {
    if (modes[current_mode].update) {
        modes[current_mode].update();
    }
}

void mode_manager_render(void) {
    if (modes[current_mode].render) {
        modes[current_mode].render();
    }
}

app_state_t mode_manager_get_app_state(void) {
    return app_state;
}

void mode_manager_set_app_state(app_state_t state) {
    app_state = state;
}
```

- [ ] **Step 2: 编译验证** — 将 mode_manager.c 加入工程。应用层全部完成。

---

## Phase 4: FreeRTOS 任务集成 + main.c

### Task 15: FreeRTOS 任务实现 (填入 freertos.c 和 main.c USER CODE 块)

**Files:**
- Modify: `firmware/Core/Src/main.c` — 在 USER CODE 块中插入初始化代码
- Modify: `firmware/Core/Src/freertos.c` — 在 Includes / Application 块中插入任务实现代码

- [ ] **Step 1: 修改 main.c — 添加 include 和初始化调用**

在 `main.c` 的 `/* USER CODE BEGIN Includes */` 后插入:
```c
/* USER CODE BEGIN Includes */
#include "pin_config.h"
#include "app_config.h"
#include "st7735.h"
#include "w25q64.h"
#include "a4988.h"
#include "button.h"
#include "temp_sensor.h"
#include "rtc_drv.h"
#include "gui.h"
#include "pointer_engine.h"
#include "fs_mgr.h"
#include "mode_manager.h"
#include "tasks.h"
/* USER CODE END Includes */
```

在 `main.c` 的 `/* USER CODE BEGIN 2 */` 后插入:
```c
/* USER CODE BEGIN 2 */
  LOG("========== OV-Watch Firmware v1.0.0 ==========\r\n");
  LOG("System Core Clock: %lu MHz\r\n", SystemCoreClock / 1000000);

  /* 1. BSP 驱动初始化 */
  st7735_init();
  button_init();
  a4988_init();
  temp_sensor_init();
  /* W25Q64 / LittleFS 延迟到后台任务初始化 (需要FreeRTOS) */

  /* 2. 中间件初始化 */
  gui_init();
  pointer_engine_init();

  /* 3. 应用层初始化 */
  mode_manager_init();

  /* 4. 启动背光 PWM */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  st7735_set_brightness(LCD_BRIGHTNESS_DEFAULT);
  LOG("Hardware initialization complete\r\n");
  /* USER CODE END 2 */
```

- [ ] **Step 2: 修改 freertos.c — 添加任务实现**

在 `freertos.c` 的 `/* USER CODE BEGIN Includes */` 后插入:
```c
/* USER CODE BEGIN Includes */
#include "main.h"
#include "cmsis_os2.h"
#include "tasks.h"
#include "button.h"
#include "mode_manager.h"
#include "pointer_engine.h"
#include "a4988.h"
#include "st7735.h"
#include "gui.h"
#include "fs_mgr.h"
#include "pin_config.h"
#include "app_config.h"
#include "temp_sensor.h"
/* USER CODE END Includes */
```

在 `freertos.c` 的 `/* USER CODE BEGIN Application */` 后插入四个任务实现:
```c
/* USER CODE BEGIN Application */

/* ---- 按键任务 (10ms 周期) ---- */
void StartTaskButton(void *argument) {
  (void)argument;
  button_init();

  for (;;) {
    button_scan();

    /* 将事件发送到后台队列 */
    if (button_has_event()) {
      button_msg_t evt = button_get_event();
      btn_msg_t msg = {
        .btn_id = (uint8_t)evt.id,
        .event = (uint8_t)evt.event,
        .timestamp = evt.timestamp_ms,
      };
      osMessageQueuePut(QueueBtnEventsHandle, &msg, 0, 0);
    }

    osDelay(pdMS_TO_TICKS(BTN_SCAN_MS));
  }
}

/* ---- 后台任务 (事件驱动 + 100ms 周期性) ---- */
void StartTaskBG(void *argument) {
  (void)argument;

  /* 延迟初始化 (需要 FreeRTOS 运行) */
  fs_mgr_init();
  LOG("Background task started, FS ready\r\n");

  uint32_t last_update = osKernelGetTickCount();

  for (;;) {
    /* 1. 处理按键事件队列 */
    btn_msg_t btn_msg;
    while (osMessageQueueGet(QueueBtnEventsHandle, &btn_msg, NULL, 0) == osOK) {
      mode_manager_handle_button((button_id_t)btn_msg.btn_id,
                                  (button_event_t)btn_msg.event);
    }

    /* 2. 检查电机到位事件 */
    if (osEventFlagsGet(EvtMotorHandle) & 0x01) {
      /* 电机到位，可触发后续动作 */
    }

    /* 3. 周期性更新 (100ms) */
    uint32_t now = osKernelGetTickCount();
    if (now - last_update >= pdMS_TO_TICKS(100)) {
      last_update = now;
      mode_manager_update();
    }

    /* 4. 渲染请求 */
    mode_manager_render();

    /* 5. 将脏矩形发送到显示任务队列 */
    if (gui_dirty_get_count() > 0) {
      dirty_rect_t merged = gui_dirty_merge();
      if (merged.valid) {
        render_msg_t cmd = {
          .cmd = RENDER_RECT,
          .param1 = merged.x,
          .param2 = merged.y,
          .param3 = merged.w,
          .param4 = merged.h,
          .color = 0,
          .ptr_or_val = 0,
        };
        osMessageQueuePut(QueueRenderCmdsHandle, &cmd, 0, 0);
      }
      gui_dirty_clear();
    }

    /* 6. 电机角度更新 */
    {
      motor_msg_t motor;
      motor.target_angle = pointer_get_current_angle();
      /* move_mode 透传 */
      osMessageQueuePut(QueueMotorTargetsHandle, &motor, 0, 0);
    }

    osDelay(pdMS_TO_TICKS(20)); /* 50Hz */
  }
}

/* ---- 显示任务 (30fps) ---- */
void StartTaskDisplay(void *argument) {
  (void)argument;

  for (;;) {
    render_msg_t cmd;
    /* 非阻塞等待渲染指令 */
    if (osMessageQueueGet(QueueRenderCmdsHandle, &cmd, NULL, 0) == osOK) {
      switch (cmd.cmd) {
        case RENDER_FULL_SCREEN:
          st7735_fill_screen((color_t)cmd.color);
          break;
        case RENDER_RECT:
          /* 局部刷新已经由模式函数直接绘制完成 */
          /* 这里只需要确认脏区域已被写入 TFT */
          break;
        default:
          break;
      }
    }
    osDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
  }
}

/* ---- 电机任务 (50ms 周期) ---- */
void StartTaskMotor(void *argument) {
  (void)argument;

  for (;;) {
    motor_msg_t target;
    /* 阻塞等待目标 */
    if (osMessageQueueGet(QueueMotorTargetsHandle, &target, NULL,
                          pdMS_TO_TICKS(POINTER_UPDATE_MS)) == osOK) {
      /* 更新指针引擎 */
      pointer_set_target(target.target_angle, POINTER_MOVE_NORMAL);
      pointer_engine_update();

      /* 转换角度为步进电机微步数 */
      float angle = pointer_get_current_angle();
      int32_t steps = (int32_t)(angle * MOTOR_TOTAL_STEPS / 360.0f);

      /* 控制 A4988 (简化: 逐步逼近) */
      motor_state_t ms = a4988_get_state();
      int32_t diff = steps - ms.current_steps;
      if (diff > 10) {
        a4988_set_direction(true);
        a4988_set_speed(MOTOR_MAX_SPEED);
      } else if (diff < -10) {
        a4988_set_direction(false);
        a4988_set_speed(MOTOR_MAX_SPEED);
      } else {
        a4988_set_speed(0);
        /* 到位标志 */
        osEventFlagsSet(EvtMotorHandle, 0x01);
      }
    }

    osDelay(pdMS_TO_TICKS(POINTER_UPDATE_MS));
  }
}

/* USER CODE END Application */
```

- [ ] **Step 3: 全项目编译链接**

```bash
# 所有源文件加入编译，确保链接通过
# 注意: LittleFS、CMSIS-DSP 等第三方库需要加入 include path
```

- [ ] **Step 4: 最终检查清单**

1. 所有 USER CODE 块正确放入 CubeMX 生成代码的预留区域
2. `#include` 路径完整，无缺失头文件
3. `-lm` (libm 数学库) 加入链接选项
4. FreeRTOS heap 足够 (configTOTAL_HEAP_SIZE = 40960)
5. LittleFS 源码路径已加入项目
6. 编译 0 error, warning 仅限 CubeMX 标准 warning

---

## 附录A: 依赖关系图

```
tasks (freertos.c + main.c)
  └── mode_manager.c
        ├── clock_mode.c  → rtc_drv.c, gui.c, pointer_engine.c
        ├── temp_mode.c   → temp_sensor.c, gui.c, pointer_engine.c
        ├── timer_mode.c  → gui.c, pointer_engine.c, a4988.c
        └── settings_mode.c → gui.c, pointer_engine.c, rtc_drv.c, fs_mgr.c
              └── fs_mgr.c   → w25q64.c, lfs (LittleFS)
                    └── gui.c       → st7735.c
                          └── pointer_engine.c → (纯计算, 无 HAL 依赖)
                                └── button.c     → (GPIO, 纯 HAL)
```

## 附录B: 编译验证命令

```bash
# STM32CubeIDE 命令行构建
/opt/st/stm32cubeide_1.x/stm32cubeide --launcher.suppressErrors \
  -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data /tmp/ws -import firmware/ -build "ov-watch/Debug"

# 或使用 arm-none-eabi-gcc 直接编译
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
  -DSTM32F407xx -DUSE_HAL_DRIVER \
  -I...所有include路径... \
  *.c -o ov-watch.elf -lm -T STM32F407ZGTx_FLASH.ld
```

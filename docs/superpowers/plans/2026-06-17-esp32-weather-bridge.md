# ESP32 天气桥接 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32 通过 UART 每 60 秒发送 NTP 时间 + 天气数据给 STM32，STM32 自动校准 RTC 并在 temp_mode 显示天气。

**Architecture:** ESP32 (ESP-IDF) 单工发送格式化帧 → STM32 USART2 DMA 循环接收 → BG 任务轮询解析 → 更新 RTC + g_weather → temp_mode UI 显示。

**Tech Stack:** ESP-IDF v5.4 (UART driver) / Keil ARMCC V5 (HAL UART + DMA + CMSIS-RTOS2)

---

## Phase 1: ESP32 固件（uart_bridge_task）

### Task 1: 创建 uart_bridge_task.h

**Files:**
- Create: `C:/Projects/weather_clock/main/tasks/uart_bridge_task.h`

- [ ] **Step 1: 写头文件**

```c
#ifndef UART_BRIDGE_TASK_H
#define UART_BRIDGE_TASK_H

void uart_bridge_init(void);
void uart_bridge_task(void *pvParameters);

#endif
```

---

### Task 2: 创建 uart_bridge_task.c

**Files:**
- Create: `C:/Projects/weather_clock/main/tasks/uart_bridge_task.c`

- [ ] **Step 1: 写完整实现**

```c
#include "uart_bridge_task.h"
#include "../shared/display_data.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define UART_BRIDGE_PORT  UART_NUM_2
#define TX_PIN            GPIO_NUM_17
#define RX_PIN            GPIO_NUM_16

/* ================================================================
 * weather_code → 英文描述 (与 STM32 协议对齐)
 * ================================================================ */
static const char *weather_desc(uint8_t code) {
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Partly_Cloudy";
        case 2:  return "Cloudy";
        case 3:  return "Rain";
        case 4:  return "Snow";
        default: return "Unknown";
    }
}

/* ================================================================
 * UART 初始化 — 115200, 8N1, TX only
 * ================================================================ */
void uart_bridge_init(void) {
    uart_config_t uart_config = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_BRIDGE_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_BRIDGE_PORT, TX_PIN, RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_BRIDGE_PORT, 256, 0, 0, NULL, 0));
}

/* ================================================================
 * 发送任务 — 每 60 秒发一帧
 * ================================================================ */
void uart_bridge_task(void *pvParameters) {
    char tx_buffer[128];

    while (1) {
        /* 等待至少一条有效天气数据 */
        if (!g_display_data.time_valid || !g_display_data.weather_valid) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* 1. 格式化时间 */
        char time_str[32];
        snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d",
                 g_display_data.year,
                 g_display_data.month,
                 g_display_data.day,
                 g_display_data.hour,
                 g_display_data.minute,
                 g_display_data.second);

        /* 2. 天气描述 (code→英文，空格→下划线) */
        const char *desc = weather_desc(g_display_data.weather_code);

        /* 3. 拼接 payload */
        char payload[80];
        snprintf(payload, sizeof(payload), "%s|%.1f|%d|%s",
                 time_str,
                 (float)g_display_data.temperature,
                 g_display_data.humidity,
                 desc);

        /* 4. 校验和 (payload 逐字节累加取低 8 位) */
        uint8_t checksum = 0;
        for (int i = 0; payload[i]; i++) {
            checksum += (uint8_t)payload[i];
        }

        /* 5. 拼接帧: $payload|XX\n */
        snprintf(tx_buffer, sizeof(tx_buffer), "$%s|%02X\n", payload, checksum);

        /* 6. 发送 */
        uart_write_bytes(UART_BRIDGE_PORT, tx_buffer, strlen(tx_buffer));

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
```

---

### Task 3: ESP32 main.c 注册 UART + 任务

**Files:**
- Modify: `C:/Projects/weather_clock/main/main.c`

- [ ] **Step 1: 在 app_main() 中创建 uart_bridge_task 之前添加 uart_bridge_init() 调用**

找到 `xTaskCreate` 各任务的位置，在创建 network_task 之后、display_task 之前插入：

```c
#include "tasks/uart_bridge_task.h"    /* 添加到文件顶部 include 区域 */

// 在 app_main() 中，network 任务创建之后、display 任务创建之前:
uart_bridge_init();
xTaskCreate(uart_bridge_task, "uart_bridge", 4096, NULL, 5, NULL);
```

---

### Task 4: CMakeLists.txt 添加源文件

**Files:**
- Modify: `C:/Projects/weather_clock/main/CMakeLists.txt`

- [ ] **Step 1: 在 SRCS 列表中添加**

```cmake
"tasks/uart_bridge_task.c"
```

- [ ] **Step 2: 编译验证**

```cmd
cd C:\Projects\weather_clock
idf.py -j2 build
```

---

## Phase 2: STM32 CubeMX 配置

### Task 5: CubeMX 启用 USART2

**Files:**
- Modify: `firmware/ov-watch.ioc` (通过 CubeMX GUI)

- [ ] **Step 1: 在 CubeMX 中打开 `ov-watch.ioc`，配置 USART2**

```
Connectivity → USART2 → Mode: Asynchronous
  Baud Rate: 115200
  Word Length: 8 Bits
  Parity: None
  Stop Bits: 1
  Overrun: Disable

Pinout:
  PD6 → USART2_RX (会自动分配)
  PD5 → USART2_TX (会自动分配，后面手动改为 ANALOG)

DMA Settings (DMA tab):
  Add → USART2_RX
    DMA Request: USART2_RX
    Stream: DMA1 Stream 5
    Direction: Peripheral to Memory
    Priority: Medium
    Mode: Circular
    Increment Address: Peripheral=off, Memory=on
    Data Width: Peripheral=Byte, Memory=Byte
```

- [ ] **Step 2: Generate Code → 确认 main.h 中出现了 huart2**

预期 main.h 自动新增：
```c
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart2_rx;
```

- [ ] **Step 3: 编译确认无新增错误**

在 Keil 中 Rebuild，预期 `huart2`、`MX_USART2_UART_Init` 等符号由 CubeMX 自动生成。

---

## Phase 3: STM32 底层驱动（USART2 + DMA）

### Task 6: 关断 USART2_TX（PD5 → ANALOG）

**Files:**
- Modify: `firmware/Core/Src/stm32f4xx_hal_msp.c`

- [ ] **Step 1: 在 USART2_MspInit（CubeMX 自动生成）中，把 PD5 改为 ANALOG**

CubeMX 生成的代码在 `HAL_UART_MspInit` 中会自动配 PD5=AF7、PD6=AF7。把 PD5 那一段改成：

```c
// 在 HAL_UART_MspInit 的 if(huart->Instance==USART2) 分支中:

/**USART2 GPIO Configuration
    PD6     ------> USART2_RX
    PD5     ------> DISABLED (TX not used, analog to prevent floating noise)
*/
GPIO_InitStruct.Pin = GPIO_PIN_6;
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

/* PD5: force ANALOG mode to disable TX output */
GPIO_InitStruct.Pin = GPIO_PIN_5;
GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
GPIO_InitStruct.Pull = GPIO_NOPULL;
HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
```

---

### Task 7: 添加 DMA1_Stream5 中断处理

**Files:**
- Modify: `firmware/Core/Src/stm32f4xx_it.c`

- [ ] **Step 1: 在文件顶部 extern 区域添加 DMA 句柄**

在 `/* USER CODE BEGIN EV */` 块中添加：
```c
extern DMA_HandleTypeDef hdma_usart2_rx;
```

- [ ] **Step 2: 在文件底部 `/* USER CODE BEGIN 1 */` 块中添加 ISR**

```c
/**
  * @brief DMA1_Stream5 IRQ handler — USART2 RX DMA 循环接收
  */
void DMA1_Stream5_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart2_rx);
}
```

---

### Task 8: main.c 中添加 DMA 接收缓冲区和 HAL_UART_Receive_DMA 调用

**Files:**
- Modify: `firmware/Core/Src/main.c`

- [ ] **Step 1: 在文件顶部的 USER CODE 私有变量区添加缓冲区**

在 `/* USER CODE BEGIN PV */` 或 `/* USER CODE BEGIN 0 */` 块中：
```c
/* ---- USART2 DMA 接收 (ESP32 天气桥接) ---- */
#include "app_config.h"  /* weather_data_t */

weather_data_t g_weather = {0};

static uint8_t  uart2_rx_buf[256];   /* DMA 循环写入 */
static char     uart2_line_buf[128]; /* 行缓冲 */
static uint8_t  uart2_line_pos;      /* 行缓冲写入位置 */
static uint32_t uart2_dma_prev;      /* 上次 DMA NDTR */
```

- [ ] **Step 2: 在 MX_USART2_UART_Init() 调用之后启动 DMA 接收**

找到 `/* USER CODE BEGIN 2 */` 中 `MX_USART2_UART_Init()` 之后，添加：
```c
/* 启动 USART2 DMA 循环接收 (ESP32 天气数据) */
HAL_UART_Receive_DMA(&huart2, uart2_rx_buf, sizeof(uart2_rx_buf));
```

---

## Phase 4: 帧解析 + 数据更新

### Task 9: 创建 weather_bridge.h

**Files:**
- Create: `firmware/App/weather_bridge.h`

- [ ] **Step 1: 写头文件**

```c
/**
 * weather_bridge.h — ESP32 UART 天气帧解析
 *
 * 解析格式: $YYYY-MM-DD HH:MM:SS|temp|hum|desc|XX\n
 */

#ifndef WEATHER_BRIDGE_H
#define WEATHER_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

void weather_bridge_poll_dma(void);
void weather_frame_parse(const char *frame);

#endif /* WEATHER_BRIDGE_H */
```

---

### Task 10: 创建 weather_bridge.c

**Files:**
- Create: `firmware/App/weather_bridge.c`

- [ ] **Step 1: 写完整帧解析 + DMA 轮询**

```c
/**
 * weather_bridge.c — ESP32 UART 天气帧解析
 *
 * 依赖: main.h (huart2, hdma_usart2_rx), app_config.h (g_weather),
 *        rtc_drv.h, gui.h, pin_config.h
 */

#include "weather_bridge.h"
#include "main.h"
#include "app_config.h"
#include "rtc_drv.h"
#include "gui.h"
#include "pin_config.h"
#include <stdio.h>
#include <string.h>

/* ---- 外部声明 (main.c 中定义) ---- */
extern UART_HandleTypeDef   huart2;
extern DMA_HandleTypeDef    hdma_usart2_rx;
extern weather_data_t       g_weather;
extern uint8_t  uart2_rx_buf[256];
extern char     uart2_line_buf[128];
extern uint8_t  uart2_line_pos;
extern uint32_t uart2_dma_prev;

/* ---- 上次渲染数据 (用于去重) ---- */
static weather_data_t last_rendered;

/* ================================================================
 * DMA 轮询 — BG 任务每 50ms 调用
 * ================================================================ */
void weather_bridge_poll_dma(void)
{
    /* 读取 DMA 当前剩余计数 */
    uint32_t current = __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
    uint32_t len;

    if (current >= uart2_dma_prev) {
        len = current - uart2_dma_prev;
    } else {
        len = (sizeof(uart2_rx_buf) - uart2_dma_prev) + current; /* 回绕 */
    }

    if (len == 0) {
        uart2_dma_prev = current;
        return;
    }

    /* 逐字节扫描新增数据 */
    uint32_t start_idx = (sizeof(uart2_rx_buf) - uart2_dma_prev) % sizeof(uart2_rx_buf);

    for (uint32_t i = 0; i < len; i++) {
        uint32_t idx = (start_idx + i) % sizeof(uart2_rx_buf);
        uint8_t byte = uart2_rx_buf[idx];

        if (byte == '$') {
            uart2_line_pos = 0;                       /* 帧头 → 开始新帧 */
        } else if (byte == '\n') {
            uart2_line_buf[uart2_line_pos] = '\0';    /* 帧尾 → 触发解析 */
            if (uart2_line_pos > 0) {
                weather_frame_parse(uart2_line_buf);
            }
            uart2_line_pos = 0;
        } else {
            if (uart2_line_pos < (sizeof(uart2_line_buf) - 1)) {
                uart2_line_buf[uart2_line_pos++] = (char)byte;
            } else {
                uart2_line_pos = 0;                   /* 溢出丢弃 */
            }
        }
    }

    uart2_dma_prev = current;
}

/* ================================================================
 * 帧解析 — 输入不含 $ 和 \n，例如:
 *   "2026-06-17 10:30:00|22.5|65|Cloud|A3"
 * ================================================================ */
void weather_frame_parse(const char *frame)
{
    /* 1. 找最后一个 '|' → 校验和 */
    const char *last_pipe = strrchr(frame, '|');
    if (!last_pipe) return;

    /* 2. 取 hex 校验和 */
    unsigned int rx_cs;
    if (sscanf(last_pipe + 1, "%2X", &rx_cs) != 1) return;

    /* 3. 计算 payload 校验和 (frame[0] ~ last_pipe-1) */
    uint8_t calc_cs = 0;
    for (const char *p = frame; p < last_pipe; p++) {
        calc_cs += (uint8_t)*p;
    }
    if (calc_cs != (uint8_t)rx_cs) return;            /* 不匹配 → 丢弃 */

    /* 4. sscanf 解析字段 (ARMCC 不支持 %hhu，humid 用 %d) */
    int year, month, day, hour, minute, second;
    float temp;
    int humid;
    char desc[32];

    int matched = sscanf(frame, "%d-%d-%d %d:%d:%d|%f|%d|%31s",
                         &year, &month, &day, &hour, &minute, &second,
                         &temp, &humid, desc);
    if (matched < 9) return;

    /* 5. 数据合法性 */
    if (year < 2000  || month < 1 || month > 12 || day < 1 || day > 31) return;
    if (hour > 23   || minute > 59  || second > 59) return;
    if (humid < 0   || humid > 100) return;

    /* 6. ESP32 发送前把空格替换为 '_'，这里恢复 */
    for (char *p = desc; *p; p++) {
        if (*p == '_') *p = ' ';
    }

    /* 7. 写入 RTC */
    {
        rtc_datetime_t dt = {0};
        dt.time.hours   = (uint8_t)hour;
        dt.time.minutes = (uint8_t)minute;
        dt.time.seconds = (uint8_t)second;
        dt.date.day     = (uint8_t)day;
        dt.date.month   = (uint8_t)month;
        dt.date.year    = (uint16_t)year;
        dt.date.weekday = 1; /* 不重要，可后续推 */

        rtc_drv_set_datetime(&dt);
    }

    /* 8. 更新 g_weather (当前与渲染同任务，无竞态，无需锁) */
    weather_data_t tmp;
    tmp.temperature      = temp;
    tmp.humidity         = (uint8_t)humid;
    strncpy(tmp.description, desc, sizeof(tmp.description) - 1);
    tmp.description[sizeof(tmp.description) - 1] = '\0';
    tmp.last_update_tick = HAL_GetTick();
    tmp.valid            = true;

    /* 9. 去重: 数据未变化不触发布局刷新 */
    if (last_rendered.valid &&
        tmp.temperature == last_rendered.temperature &&
        tmp.humidity    == last_rendered.humidity &&
        strcmp(tmp.description, last_rendered.description) == 0) {
        /* 仅更新 last_update_tick (不影响重绘) */
        g_weather.last_update_tick = tmp.last_update_tick;
        return;
    }

    /* 10. 原子写 g_weather + 标记脏区 (同任务，无需临界区) */
    g_weather = tmp;
    last_rendered = tmp;

    /* 标记 temp_mode 天气主页区域为脏 */
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

---

## Phase 5: 数据结构 + 引脚宏

### Task 11: app_config.h 添加 weather_data_t

**Files:**
- Modify: `firmware/Core/Inc/app_config.h`

- [ ] **Step 1: 在文件末尾（`#endif` 之前）添加**

```c
/* ================================================================
 * ESP32 天气数据 (UART 桥接收)
 * ================================================================ */
typedef struct {
    float    temperature;       /* 气温 °C                       */
    uint8_t  humidity;          /* 湿度 %                        */
    char     description[32];   /* 天气描述 "Cloud", "Rain" 等   */
    uint32_t last_update_tick;  /* 最后一帧的时间戳 (HAL_GetTick) */
    bool     valid;             /* 是否收到过至少一帧             */
} weather_data_t;

extern weather_data_t g_weather;
```

---

### Task 12: pin_config.h 添加 ESP32 UART 宏

**Files:**
- Modify: `firmware/Core/Inc/pin_config.h`

- [ ] **Step 1: 在串口调试区之后添加**

```c
/* ================================================================
 * ESP32 天气桥接 UART
 * ================================================================ */
#define ESP32_UART          USART2
#define ESP32_UART_BAUD     115200
#define ESP32_RX_PIN        GPIO_PIN_6
#define ESP32_RX_PORT       GPIOD
```

---

## Phase 6: freertos.c BG 任务集成

### Task 13: BG 任务 DMA 轮询 + weather_frame_parse 调用

**Files:**
- Modify: `firmware/Core/Src/freertos.c`

- [ ] **Step 1: 在 BG 任务循环中添加 UART 轮询**

在 `StartTaskBG` 的 `for (;;)` 循环中，按键处理之前添加：

```c
    /* 0. ESP32 UART 天气数据轮询 (DMA 循环接收) */
    weather_bridge_poll_dma();
```

- [ ] **Step 2: 在文件顶部 include 区域添加**

```c
#include "weather_bridge.h"
```

确保 `#include "weather_bridge.h"` 在 `/* USER CODE END Includes */` 之前。

---

## Phase 7: temp_mode UI 重写

### Task 14: 重写 temp_mode.c

**Files:**
- Modify: `firmware/App/modes/temp_mode.c`

- [ ] **Step 1: 替换整个 temp_mode.c**

```c
/**
 * temp_mode.c — 天气模式 (ESP32 数据) + 设备信息页
 *
 * 短按 → 模式管理器切下一个模式 (本文件不处理)
 * 长按 → 切换 show_device_info (天气主页 ↔ 设备信息页)
 *
 * 天气主页数据来源: g_weather (ESP32 UART 桥接)
 * 设备信息: 内部 ADC、Flash 状态、ESP32 连接状态
 */

#include "temp_mode.h"
#include "temp_sensor.h"
#include "gui.h"
#include "pointer_engine.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include "main.h"
#include "rtc_drv.h"
#include "fs_mgr.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 静态状态
 * ================================================================ */
static bool fahrenheit = false;
static bool show_device_info = false;     /* false=天气主页, true=设备信息 */
static bool needs_render_device_info = false; /* 设备信息页脏标志 */

/* 天气主页渲染去重 */
static weather_data_t last_rendered;

/* ================================================================
 * 公共 API
 * ================================================================ */

void temp_mode_init(void)
{
    fahrenheit = false;
    show_device_info = false;
    memset(&last_rendered, 0, sizeof(last_rendered));
}

void temp_mode_enter(void)
{
    LOG("TEMP: enter");
    show_device_info = false;               /* 每次进入重置为天气主页 */
    needs_render_device_info = true;
    memset(&last_rendered, 0, sizeof(last_rendered));
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void temp_mode_exit(void)
{
    LOG("TEMP: exit");
}

void temp_mode_update(void)
{
    /* 天气数据由 ESP32 UART 推送，无需主动轮询 */
    pointer_set_temperature(g_weather.temperature, fahrenheit);
}

/* ================================================================
 * 渲染
 * ================================================================ */

static void render_weather_page(void)
{
    /* 去重: 数据未变不刷新 */
    if (last_rendered.valid &&
        g_weather.temperature == last_rendered.temperature &&
        g_weather.humidity    == last_rendered.humidity &&
        strcmp(g_weather.description, last_rendered.description) == 0) {
        return;
    }
    last_rendered = g_weather;

    /* 清屏 */
    st7735_fill_screen(COLOR_BLACK);

    /* ---- 1. 天气图标 + 描述 ---- */
    {
        char buf[32];
        gui_icon_t icon = ICON_SUN;

        if (strstr(g_weather.description, "Cloud"))  icon = ICON_MOON;
        if (strstr(g_weather.description, "Rain"))   icon = ICON_CROSS;
        if (strstr(g_weather.description, "Snow"))   icon = ICON_ARROW_DOWN;

        gui_draw_icon(4, 5, icon, COLOR_YELLOW);
        snprintf(buf, sizeof(buf), "%s", g_weather.description);
        st7735_draw_text(24, 5, buf, FONT_8x16, COLOR_CYAN, COLOR_BLACK);
    }

    /* ---- 2. 大字温度 ---- */
    {
        char buf[16];
        float t = g_weather.temperature;

        if (fahrenheit) {
            t = t * 9.0f / 5.0f + 32.0f;
        }

        snprintf(buf, sizeof(buf), "%.1f", t);
        gui_draw_text_centered(LCD_WIDTH / 2, 50, buf, 2,   /* FONT_12x24 */
                               COLOR_WHITE, COLOR_BLACK);

        snprintf(buf, sizeof(buf), "%s", fahrenheit ? "oF" : "oC");
        gui_draw_text_centered(LCD_WIDTH / 2, 80, buf, 0,   /* FONT_6x8 */
                               COLOR_GRAY, COLOR_BLACK);
    }

    /* ---- 3. 湿度 ---- */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "H:%u%%", g_weather.humidity);
        st7735_draw_text(10, 105, buf, FONT_8x16, COLOR_CYAN, COLOR_BLACK);
    }

    /* ---- 4. 时间 (NTP 校准后的 RTC) ---- */
    {
        rtc_datetime_t dt = rtc_drv_get_datetime();
        char buf[16];
        snprintf(buf, sizeof(buf), "%02u:%02u",
                 dt.time.hours, dt.time.minutes);
        gui_draw_text_aligned(130, buf, 1,             /* FONT_8x16 */
                              COLOR_WHITE, COLOR_BLACK, GUI_ALIGN_RIGHT);
    }

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

static void render_device_info_page(void)
{
    if (!needs_render_device_info) return;
    needs_render_device_info = false;

    st7735_fill_screen(COLOR_BLACK);

    /* ---- 标题 ---- */
    gui_draw_text_centered(LCD_WIDTH / 2, 5, "Device Info",
                           0, COLOR_WHITE, COLOR_BLACK);

    /* ---- STM32 内部 ADC 温度 ---- */
    {
        temp_data_t d = temp_sensor_read();
        char buf[32];
        snprintf(buf, sizeof(buf), "STM32: %.1fC", d.temperature);
        st7735_draw_text(5, 30, buf, FONT_8x16, COLOR_GRAY, COLOR_BLACK);
    }

    /* ---- ESP32 连接状态 ---- */
    {
        const char *status;
        uint16_t color;

        if (!g_weather.valid) {
            status = "ESP32: No Data";
            color  = COLOR_RED;
        } else {
            uint32_t elapsed_s = (HAL_GetTick() - g_weather.last_update_tick) / 1000;
            if (elapsed_s <= 120) {
                status = "ESP32: Connected";
                color  = COLOR_GREEN;
            } else if (elapsed_s <= 600) {
                status = "ESP32: Weak";
                color  = COLOR_YELLOW;
            } else {
                status = "ESP32: Lost";
                color  = COLOR_RED;
            }
        }
        st7735_draw_text(5, 55, status, FONT_8x16, color, COLOR_BLACK);
    }

    /* ---- RTC 日期 ---- */
    {
        rtc_datetime_t dt = rtc_drv_get_datetime();
        char buf[32];
        snprintf(buf, sizeof(buf), "RTC: %04u-%02u-%02u",
                 dt.date.year, dt.date.month, dt.date.day);
        st7735_draw_text(5, 80, buf, FONT_8x16, COLOR_GRAY, COLOR_BLACK);
    }

    /* ---- Flash 状态 ---- */
    {
        char buf[32];
        bool mounted = fs_mgr_is_mounted();
        uint32_t free_kb = fs_get_free_config() / 1024;
        snprintf(buf, sizeof(buf), "Flash: %s %luKB",
                 mounted ? "OK" : "NO", (unsigned long)free_kb);
        st7735_draw_text(5, 105, buf, FONT_8x16, COLOR_GRAY, COLOR_BLACK);
    }

    /* ---- 底部提示 ---- */
    gui_draw_text_centered(LCD_WIDTH / 2, 145, "HOLD: back",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void temp_mode_render(void)
{
    if (show_device_info) {
        render_device_info_page();
    } else {
        render_weather_page();
    }
}

/* ================================================================
 * 按键
 * ================================================================ */

void temp_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event == BTN_EVENT_LONG_PRESS) {
        show_device_info = !show_device_info;
        needs_render_device_info = true;
        memset(&last_rendered, 0, sizeof(last_rendered));
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        LOG("TEMP: show_device_info=%d", show_device_info);
    }
}

bool temp_mode_is_fahrenheit(void)
{
    return fahrenheit;
}
```

---

## Phase 8: 编译 & 回归验证

### Task 15: 编译 + 检查

- [ ] **Step 1: Keil Rebuild**

预期：0 Error。可能有 `weather_bridge.c` 中 `uart2_line_buf` 等 extern 的 Warning（keil 对 extern 数组不严格要求）。

- [ ] **Step 2: 运行回归检查**

```bash
bash .claude/scripts/check-firmware.sh
```

预期：18 项全部通过。

- [ ] **Step 3: 检查新增文件是否已加入 Keil 工程**

Keil → Project → Manage → Project Items →
  `Application/User/App/` 组 → Add `weather_bridge.c`

---

## Phase 9: 接线 & 上电验证

### Task 16: 接 ESP32 → STM32

- [ ] **Step 1: 3 根线**

```
ESP32 GPIO17 ── PD6 (USART2_RX)
ESP32 GND    ── 面包板 -轨
ESP32 VIN    ── 面包板 +轨(5V)  ← 注意是 5V，不是 3.3V！
```

- [ ] **Step 2: ESP32 先烧录 weather_clock（含 uart_bridge），STM32 再烧录 ov-watch**

- [ ] **Step 3: 串口验证**

STM32 调试串口应出现：
```
[LOG] g_weather valid, temp=22.5, hum=65, desc=Cloud
```

- [ ] **Step 4: 屏幕验证**

短按切换到 temp_mode → 天气主页显示温度/湿度/时间。

---

## 文件变更总表

| Phase | 文件 | 操作 |
|-------|------|------|
| 1 | `weather_clock/main/tasks/uart_bridge_task.h` | 新建 |
| 1 | `weather_clock/main/tasks/uart_bridge_task.c` | 新建 |
| 1 | `weather_clock/main/main.c` | 修改 (+6行) |
| 1 | `weather_clock/main/CMakeLists.txt` | 修改 (+1行) |
| 2 | `firmware/ov-watch.ioc` | CubeMX 重配 |
| 3 | `firmware/Core/Src/stm32f4xx_hal_msp.c` | 修改 (PD5→ANALOG) |
| 3 | `firmware/Core/Src/stm32f4xx_it.c` | 修改 (+6行) |
| 3 | `firmware/Core/Src/main.c` | 修改 (+20行) |
| 4 | `firmware/App/weather_bridge.h` | 新建 |
| 4 | `firmware/App/weather_bridge.c` | 新建 (~140行) |
| 5 | `firmware/Core/Inc/app_config.h` | 修改 (+12行) |
| 5 | `firmware/Core/Inc/pin_config.h` | 修改 (+6行) |
| 6 | `firmware/Core/Src/freertos.c` | 修改 (+3行) |
| 7 | `firmware/App/modes/temp_mode.c` | 重写 (~200行) |
| 8 | Keil 工程 | 添加 weather_bridge.c |

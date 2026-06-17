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
    static int first = 1;
    if (first) {
        LOG("weather_bridge DMA poll started\r\n");
        first = 0;
    }

    /*
     * __HAL_DMA_GET_COUNTER 返回 NDTR (剩余传输数，往下减)
     * DMA 写入位置 = (buf_size - ndtr) % buf_size
     */
    uint32_t ndtr   = __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
    uint32_t dma_pos = (sizeof(uart2_rx_buf) - ndtr) % sizeof(uart2_rx_buf);
    uint32_t len;

    if (dma_pos >= uart2_dma_prev) {
        len = dma_pos - uart2_dma_prev;              /* 未回绕 */
    } else {
        len = (sizeof(uart2_rx_buf) - uart2_dma_prev) + dma_pos; /* 回绕 */
    }

    if (len == 0) return;
    if (len > sizeof(uart2_rx_buf)) {                /* 异常：漏了太多，丢弃 */
        uart2_dma_prev = dma_pos;
        return;
    }

    /* 从上次停止处开始扫描新增字节 */
    for (uint32_t i = 0; i < len; i++) {
        uint32_t idx = (uart2_dma_prev + i) % sizeof(uart2_rx_buf);
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

    uart2_dma_prev = dma_pos;
}

/* ================================================================
 * 帧解析 — 输入不含 $ 和 \n，例如:
 *   "2026-06-17 10:30:00|22.5|65|Cloud|A3"
 * ================================================================ */
void weather_frame_parse(const char *frame)
{
    LOG("weather frame: %s\r\n", frame);

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

    /* 截断校验和部分: 将最后一个 '|' 替换为 '\0'，使 sscanf 不读到 |D5 */
    ((char *)last_pipe)[0] = '\0';

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
        dt.date.weekday = 1; /* 不重要，可后续推算 */

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

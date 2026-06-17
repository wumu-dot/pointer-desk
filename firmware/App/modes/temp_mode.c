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
static bool show_device_info = false;       /* false=天气主页, true=设备信息 */
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
    /* 天气数据由 ESP32 UART 推送，无需主动轮询传感器 */
    pointer_set_temperature(g_weather.temperature, fahrenheit);
}

/* ================================================================
 * 渲染
 * ================================================================ */

static void render_weather_page(void)
{
    /* 无有效数据：显示等待提示 */
    if (!g_weather.valid) {
        if (last_rendered.valid) {
            /* 状态从 valid→invalid，需要重绘 */
            memset(&last_rendered, 0, sizeof(last_rendered));
        } else {
            return; /* 已显示等待状态，不重复刷新 */
        }
        st7735_fill_screen(COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, LCD_HEIGHT / 2,
                               "Waiting ESP32...", 0,
                               COLOR_GRAY, COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        return;
    }

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
        gui_icon_t icon = ICON_SUN;
        const char *d = g_weather.description;

        if (strstr(d, "Cloud"))  icon = ICON_MOON;
        if (strstr(d, "Rain"))   icon = ICON_CROSS;
        if (strstr(d, "Snow"))   icon = ICON_ARROW_DOWN;

        gui_draw_icon(4, 5, icon, COLOR_YELLOW);
        st7735_draw_text(24, 5, (char *)d, FONT_8x16, COLOR_CYAN, COLOR_BLACK);
    }

    /* ---- 2. 大字温度 ---- */
    {
        char buf[16];
        float t = g_weather.temperature;

        if (fahrenheit) {
            t = t * 9.0f / 5.0f + 32.0f;
        }

        snprintf(buf, sizeof(buf), "%.1f", t);
        gui_draw_text_centered(LCD_WIDTH / 2, 50, buf, 2,   /* font_id=2 → FONT_12x24 */
                               COLOR_WHITE, COLOR_BLACK);

        snprintf(buf, sizeof(buf), "%s", fahrenheit ? "oF" : "oC");
        gui_draw_text_centered(LCD_WIDTH / 2, 80, buf, 0,   /* font_id=0 → FONT_6x8 */
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
        gui_draw_text_aligned(130, buf, 1,             /* font_id=1 → FONT_8x16 */
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

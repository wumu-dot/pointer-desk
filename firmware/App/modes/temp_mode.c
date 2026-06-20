/**
 * temp_mode.c — 天气/温湿度模式 (4 页子页轮转)
 *
 * 长按 → 4 页轮转: 天气主页 → 湿度计 → 双温对比 → 设备信息
 * 短按 → mode_manager 切下一个模式
 *
 * 天气主页:    ESP32 区域天气数据 (g_weather)
 * 湿度计:      SHT30 本地湿度 + 指针驱动
 * 双温对比:    LOCAL (SHT30) vs REGIONAL (ESP32)
 * 设备信息:    传感器状态、Flash 状态、ESP32 连接状态
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
 * 子页定义
 * ================================================================ */
typedef enum {
    TEMP_PAGE_WEATHER = 0,   /* 天气主页 (区域 ESP32 数据) */
    TEMP_PAGE_HUMIDITY,      /* 湿度计 (SHT30 本地湿度 + 指针) */
    TEMP_PAGE_COMPARISON,    /* 本地 SHT30 vs 区域 ESP32 对比 */
    TEMP_PAGE_DEVICE,        /* 设备信息 */
    TEMP_PAGE_COUNT
} temp_page_t;

/* ================================================================
 * 静态状态
 * ================================================================ */
static temp_page_t current_page = TEMP_PAGE_WEATHER;
static bool        fahrenheit   = false;

/* 天气主页渲染去重 */
static weather_data_t last_rendered_weather;

/* ================================================================
 * 公共 API
 * ================================================================ */

void temp_mode_init(void)
{
    fahrenheit = false;
    current_page = TEMP_PAGE_WEATHER;
    memset(&last_rendered_weather, 0, sizeof(last_rendered_weather));
}

void temp_mode_enter(void)
{
    LOG("TEMP: enter");
    current_page = TEMP_PAGE_WEATHER;
    memset(&last_rendered_weather, 0, sizeof(last_rendered_weather));
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void temp_mode_exit(void)
{
    LOG("TEMP: exit");
}

void temp_mode_update(void)
{
    temp_data_t local;

    switch (current_page) {
    case TEMP_PAGE_WEATHER:
        /* ESP32 区域温度驱动指针 */
        pointer_set_temperature(g_weather.temperature, fahrenheit);
        break;

    case TEMP_PAGE_HUMIDITY:
        /* SHT30 本地湿度驱动指针, 离线时不动 */
        local = temp_sensor_read();
        if (local.source == TEMP_SRC_SHT30) {
            uint8_t hum_pct = (uint8_t)(local.humidity + 0.5f);
            if (hum_pct > 100) hum_pct = 100;
            pointer_set_humidity(hum_pct);
        }
        break;

    case TEMP_PAGE_COMPARISON:
        /* SHT30 本地温度指针, 离线回退 ESP32 */
        local = temp_sensor_read();
        if (local.source == TEMP_SRC_SHT30) {
            pointer_set_temperature(local.temperature, fahrenheit);
        } else if (g_weather.valid) {
            pointer_set_temperature(g_weather.temperature, fahrenheit);
        }
        break;

    case TEMP_PAGE_DEVICE:
        /* 设备信息页无需驱动指针 */
        break;
    }
}

/* ================================================================
 * 渲染 — 天气主页
 * ================================================================ */

static void render_weather_page(void)
{
    if (!g_weather.valid) {
        if (last_rendered_weather.valid) {
            memset(&last_rendered_weather, 0, sizeof(last_rendered_weather));
        } else {
            return;
        }
        st7735_fill_screen(COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, LCD_HEIGHT / 2,
                               "Waiting ESP32...", 0,
                               COLOR_GRAY, COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        return;
    }

    /* 去重: 数据未变不刷新 */
    if (last_rendered_weather.valid &&
        g_weather.temperature == last_rendered_weather.temperature &&
        g_weather.humidity    == last_rendered_weather.humidity &&
        strcmp(g_weather.description, last_rendered_weather.description) == 0) {
        return;
    }
    last_rendered_weather = g_weather;

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
        gui_draw_text_centered(LCD_WIDTH / 2, 50, buf, 2,
                               COLOR_WHITE, COLOR_BLACK);

        snprintf(buf, sizeof(buf), "%s", fahrenheit ? "oF" : "oC");
        gui_draw_text_centered(LCD_WIDTH / 2, 80, buf, 0,
                               COLOR_GRAY, COLOR_BLACK);
    }

    /* ---- 3. 湿度 (ESP32 区域) ---- */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "H:%u%%", g_weather.humidity);
        st7735_draw_text(10, 105, buf, FONT_8x16, COLOR_CYAN, COLOR_BLACK);
    }

    /* ---- 4. 时间 ---- */
    {
        rtc_datetime_t dt = rtc_drv_get_datetime();
        char buf[16];
        snprintf(buf, sizeof(buf), "%02u:%02u",
                 dt.time.hours, dt.time.minutes);
        gui_draw_text_aligned(130, buf, 1,
                              COLOR_WHITE, COLOR_BLACK, GUI_ALIGN_RIGHT);
    }

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

/* ================================================================
 * 渲染 — 湿度计子页 (SHT30 本地湿度)
 * ================================================================ */

static void render_humidity_page(void)
{
    temp_data_t local   = temp_sensor_read();
    bool        sht30_ok = (local.source == TEMP_SRC_SHT30);
    uint8_t     hum_pct;

    hum_pct = (uint8_t)(local.humidity + 0.5f);
    if (hum_pct > 100) hum_pct = 100;

    /* 去重: 湿度变化或传感器状态变化才重绘 */
    {
        static uint8_t  last_humid  = 0xFF;
        static bool     last_sht30  = false;
        static bool     first_render = true;

        if (!first_render &&
            hum_pct == last_humid &&
            sht30_ok == last_sht30) {
            return;
        }
        first_render = false;
        last_humid   = hum_pct;
        last_sht30   = sht30_ok;
    }

    st7735_fill_screen(COLOR_BLACK);

    if (sht30_ok) {
        const char *label;
        uint16_t    color;

        /* 湿度等级 */
        if (hum_pct < 30)      { label = "Dry";     color = COLOR_YELLOW; }
        else if (hum_pct < 60) { label = "Comfort"; color = COLOR_GREEN;  }
        else if (hum_pct < 80) { label = "Humid";   color = COLOR_CYAN;   }
        else                   { label = "Wet";     color = COLOR_BLUE;   }

        /* 大字湿度 */
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u%%", hum_pct);
            gui_draw_text_centered(LCD_WIDTH / 2, 40, buf, 3,
                                   COLOR_WHITE, COLOR_BLACK);
        }

        /* 湿度等级标签 */
        gui_draw_text_centered(LCD_WIDTH / 2, 85, label, 1,
                               color, COLOR_BLACK);

        /* 湿度环 */
        {
            int16_t end_deg = (int16_t)((float)hum_pct * 360.0f / 100.0f);
            if (end_deg < 1) end_deg = 1;
            gui_draw_arc(64, 125, 30, 0, end_deg, 4, color);
        }
    } else {
        /* SHT30 离线降级 */
        gui_draw_text_centered(LCD_WIDTH / 2, 40, "--%", 3,
                               COLOR_DARK_GRAY, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 85, "No SHT30", 1,
                               COLOR_GRAY, COLOR_BLACK);
    }

    /* 底部提示 */
    gui_draw_text_centered(LCD_WIDTH / 2, 150, "HOLD: next",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

/* ================================================================
 * 渲染 — 双温对比子页 (LOCAL=SHT30 vs REGIONAL=ESP32)
 * ================================================================ */

static void render_comparison_page(void)
{
    temp_data_t local    = temp_sensor_read();
    bool        sht30_ok = (local.source == TEMP_SRC_SHT30);
    bool        esp32_ok = g_weather.valid;

    /* 去重 */
    {
        static float   last_local_temp = -999.0f;
        static float   last_local_hum  = -1.0f;
        static float   last_esp_temp   = -999.0f;
        static uint8_t last_esp_hum    = 0xFF;
        static bool    first_render    = true;

        if (!first_render &&
            local.temperature     == last_local_temp &&
            local.humidity        == last_local_hum  &&
            g_weather.temperature == last_esp_temp   &&
            g_weather.humidity    == last_esp_hum) {
            return;
        }
        first_render    = false;
        last_local_temp = local.temperature;
        last_local_hum  = local.humidity;
        last_esp_temp   = g_weather.temperature;
        last_esp_hum    = g_weather.humidity;
    }

    st7735_fill_screen(COLOR_BLACK);

    /* ---- 列标题 ---- */
    st7735_draw_text(5,  5, "LOCAL",    FONT_8x16, COLOR_CYAN,   COLOR_BLACK);
    st7735_draw_text(70, 5, "REGIONAL", FONT_8x16, COLOR_YELLOW, COLOR_BLACK);

    /* ---- 分隔线 ---- */
    st7735_fill_rect(64, 22, 1, 110, COLOR_DARK_GRAY);

    /* ---- 本地温度 ---- */
    {
        char buf[16];
        const char *src;
        uint16_t temp_color;

        if (sht30_ok) {
            float t = fahrenheit
                ? (local.temperature * 9.0f / 5.0f + 32.0f)
                : local.temperature;
            snprintf(buf, sizeof(buf), "%.1f", t);
            src = "SHT30";
            temp_color = COLOR_WHITE;
        } else {
            snprintf(buf, sizeof(buf), "--.-");
            src = "None";
            temp_color = COLOR_DARK_GRAY;
        }
        gui_draw_text_centered(32, 35, buf, 2, temp_color, COLOR_BLACK);
        st7735_draw_text(18, 65, src, FONT_6x8, COLOR_GRAY, COLOR_BLACK);
    }

    /* ---- 区域温度 ---- */
    {
        char buf[16];
        if (esp32_ok) {
            float t = fahrenheit
                ? (g_weather.temperature * 9.0f / 5.0f + 32.0f)
                : g_weather.temperature;
            snprintf(buf, sizeof(buf), "%.1f", t);
        } else {
            snprintf(buf, sizeof(buf), "--.-");
        }
        gui_draw_text_centered(96, 35, buf, 2, COLOR_WHITE, COLOR_BLACK);
        st7735_draw_text(80, 65, "ESP32", FONT_6x8, COLOR_GRAY, COLOR_BLACK);
    }

    /* ---- 湿度对比 ---- */
    {
        char buf[32];
        char local_hum_buf[8];

        if (sht30_ok) {
            snprintf(local_hum_buf, sizeof(local_hum_buf),
                     "%.0f", local.humidity);
            snprintf(buf, sizeof(buf), "H:%s%%", local_hum_buf);
        } else {
            snprintf(buf, sizeof(buf), "H:--%%");
        }
        st7735_draw_text(5, 95, buf, FONT_8x16, COLOR_CYAN, COLOR_BLACK);

        snprintf(buf, sizeof(buf), "H:%u%%",
                 esp32_ok ? g_weather.humidity : 0);
        st7735_draw_text(70, 95, buf, FONT_8x16, COLOR_YELLOW, COLOR_BLACK);
    }

    /* ---- 温差 ---- */
    if (sht30_ok && esp32_ok) {
        float diff = local.temperature - g_weather.temperature;
        char buf[32];
        uint16_t diff_color;

        snprintf(buf, sizeof(buf), "Diff: %+.1fC", diff);
        if (diff > 1.0f) {
            diff_color = COLOR_RED;
        } else if (diff < -1.0f) {
            diff_color = COLOR_BLUE;
        } else {
            diff_color = COLOR_GREEN;
        }
        gui_draw_text_centered(LCD_WIDTH / 2, 125, buf, 1,
                               diff_color, COLOR_BLACK);
    }

    /* 底部提示 */
    gui_draw_text_centered(LCD_WIDTH / 2, 150, "HOLD: next",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

/* ================================================================
 * 渲染 — 设备信息页
 * ================================================================ */

static void render_device_info_page(void)
{
    /* 设备信息页每次进入重绘 (数据随时变化) */
    st7735_fill_screen(COLOR_BLACK);

    /* ---- 标题 ---- */
    gui_draw_text_centered(LCD_WIDTH / 2, 5, "Device Info",
                           0, COLOR_WHITE, COLOR_BLACK);

    /* ---- 温度传感器 ---- */
    {
        temp_data_t d = temp_sensor_read();
        char buf[32];
        const char *label = temp_sensor_get_label();
        snprintf(buf, sizeof(buf), "%s: %.1fC", label, d.temperature);
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
            uint32_t elapsed_s = (HAL_GetTick() - g_weather.last_update_tick)
                                 / 1000;
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
    gui_draw_text_centered(LCD_WIDTH / 2, 145, "HOLD: next",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

/* ================================================================
 * 渲染分派
 * ================================================================ */

void temp_mode_render(void)
{
    switch (current_page) {
    case TEMP_PAGE_WEATHER:    render_weather_page();    break;
    case TEMP_PAGE_HUMIDITY:   render_humidity_page();   break;
    case TEMP_PAGE_COMPARISON: render_comparison_page(); break;
    case TEMP_PAGE_DEVICE:     render_device_info_page();break;
    }
}

/* ================================================================
 * 按键
 * ================================================================ */

void temp_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event == BTN_EVENT_LONG_PRESS) {
        /* 长按轮转 4 页 */
        current_page = (temp_page_t)((current_page + 1) % TEMP_PAGE_COUNT);
        memset(&last_rendered_weather, 0, sizeof(last_rendered_weather));
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        LOG("TEMP: page=%d", current_page);
    }
}

bool temp_mode_is_fahrenheit(void)
{
    return fahrenheit;
}

/**
 * clock_mode.c — 时钟模式 (默认启动模式)
 *
 * 显示当前日期、时间、AM/PM图标、24小时浓缩进度条。
 * 支持12/24小时制切换，同步驱动指针引擎。
 */

#include "clock_mode.h"
#include "rtc_drv.h"
#include "gui.h"
#include "pointer_engine.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 静态变量
 * ================================================================ */
static bool use_24h = false;

/* 渲染去重缓存 + 首次渲染标志 (替代0xFF哨兵) */
static struct {
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
    uint8_t  weekday;
    bool     is_pm;
    bool     show_24h;
} cached = { 0, 0, 0, 0, 0, 0, 0, false, false };
static bool need_full_refresh = true;

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

/**
 * @brief 判断显示内容是否与缓存一致
 */
static bool display_changed(const rtc_datetime_t *dt, bool pm)
{
    if (need_full_refresh)                         return true;
    if (use_24h != cached.show_24h)               return true;
    if (dt->time.hours != cached.hours)           return true;
    if (dt->time.minutes != cached.minutes)       return true;
    if (dt->time.seconds != cached.seconds)       return true;
    if (dt->date.day != cached.day)               return true;
    if (dt->date.month != cached.month)           return true;
    if (dt->date.year != cached.year)             return true;
    if (dt->date.weekday != cached.weekday)       return true;
    if (pm != cached.is_pm)                       return true;
    return false;
}

/** 更新缓存并清除强制刷新标志 */
static void update_cache(const rtc_datetime_t *dt, bool pm)
{
    cached.hours    = dt->time.hours;
    cached.minutes  = dt->time.minutes;
    cached.seconds  = dt->time.seconds;
    cached.day      = dt->date.day;
    cached.month    = dt->date.month;
    cached.year     = dt->date.year;
    cached.weekday  = dt->date.weekday;
    cached.is_pm    = pm;
    cached.show_24h = use_24h;
    need_full_refresh = false;
}

/**
 * @brief 标记整个屏幕为脏区，触发全屏刷新
 */
static void trigger_full_refresh(void)
{
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    need_full_refresh = true;
}

/* ================================================================
 * 公共 API
 * ================================================================ */

void clock_mode_init(void)
{
    rtc_format_t fmt = rtc_drv_get_format();
    use_24h = (fmt == RTC_FORMAT_24H);
    LOG("CLOCK: init, format=%s", use_24h ? "24h" : "12h");
    need_full_refresh = true;
}

void clock_mode_enter(void)
{
    LOG("CLOCK: enter");
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    need_full_refresh = true;
}

void clock_mode_exit(void)
{
    LOG("CLOCK: exit");
    /* 无需清理资源 */
}

void clock_mode_update(void)
{
    rtc_datetime_t dt = rtc_drv_get_datetime();

    if (use_24h) {
        pointer_set_clock_24h(dt.time.hours, dt.time.minutes);
    } else {
        /* 指针引擎使用24h制进行步进电机控制 */
        uint8_t hour24 = dt.time.hours;
        pointer_set_clock(hour24, dt.time.minutes);
    }
}

void clock_mode_render(void)
{
    rtc_datetime_t dt = rtc_drv_get_datetime();
    bool pm = false;

    if (!use_24h) {
        pm = rtc_drv_is_pm();
    }

    /* 若无变化则跳过渲染 */
    if (!display_changed(&dt, pm)) {
        return;
    }

    update_cache(&dt, pm);

    /* ================================================================
     * 1. AM/PM 图标 (右上角)
     * ================================================================ */
    if (!use_24h) {
        gui_icon_t icon = pm ? ICON_MOON : ICON_SUN;
        gui_draw_icon(LCD_WIDTH - 14, 4, icon, COLOR_YELLOW);
        /* gui_draw_icon() 内部已调用 gui_dirty_mark() */
    } else {
        /* 24h模式：清除图标区域 */
        st7735_fill_rect(LCD_WIDTH - 16, 0, 16, 20, COLOR_BLACK);
        gui_dirty_mark(LCD_WIDTH - 16, 0, 16, 20);
    }

    /* ================================================================
     * 2. 时间显示 HH:MM:SS (居中)
     * ================================================================ */
    {
        char time_str[16];
        uint8_t display_hour;

        if (use_24h) {
            display_hour = dt.time.hours;
        } else {
            display_hour = rtc_drv_get_hour12();
        }

        snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
                 display_hour, dt.time.minutes, dt.time.seconds);

        /* gui_draw_text_centered() 内部已调用 gui_dirty_mark() */
        gui_draw_text_centered(LCD_WIDTH / 2, 40, time_str,
                               2,                /* font_id=2 → FONT_12x24 */
                               COLOR_WHITE, COLOR_BLACK);
    }

    /* ================================================================
     * 3. 日期显示 YYYY-MM-DD 周X (居中)
     * ================================================================ */
    {
        char date_str[32];
        const char *weekday = rtc_drv_weekday_str(dt.date.weekday);

        snprintf(date_str, sizeof(date_str), "%04u-%02u-%02u %s",
                 dt.date.year, dt.date.month, dt.date.day,
                 weekday ? weekday : "");

        /* gui_draw_text_centered() 内部已调用 gui_dirty_mark() */
        gui_draw_text_centered(LCD_WIDTH / 2, 70, date_str,
                               0,                /* font_id=0 → FONT_6x8 */
                               COLOR_GRAY, COLOR_BLACK);
    }

    /* ================================================================
     * 4. 24小时浓缩进度条 (底部)
     * ================================================================ */
    {
        uint16_t bar_x = 5;
        uint16_t bar_y = 140;
        uint16_t bar_w = LCD_WIDTH - 10;
        uint16_t bar_h = 4;

        /* 进度计算: 已过分钟数 / 全天分钟数 */
        uint16_t total_minutes = (uint16_t)dt.time.hours * 60 + dt.time.minutes;
        uint16_t dot_pos = (uint16_t)((uint32_t)bar_w * total_minutes / 1440);

        /* 背景条 */
        st7735_fill_rect(bar_x, bar_y, bar_w, bar_h, COLOR_DARK_GRAY);

        /* 已过部分 (白色点, 3px宽) */
        if (dot_pos > 0) {
            /* dot_pos 不会超过 bar_w，但为安全起见限制一下 */
            if (dot_pos < 3) {
                dot_pos = 3;
            }
            st7735_fill_rect(bar_x, bar_y, dot_pos, bar_h, COLOR_WHITE);
        }

        gui_dirty_mark(bar_x, bar_y, bar_w, bar_h);
    }
}

void clock_mode_handle_button(button_id_t btn, button_event_t event)
{
    /* UP或DOWN短按：切换 12/24 小时制 */
    if (event == BTN_EVENT_SHORT_PRESS) {
        if (btn == BTN_UP || btn == BTN_DOWN) {
            use_24h = !use_24h;

            rtc_format_t fmt = use_24h ? RTC_FORMAT_24H : RTC_FORMAT_12H;
            rtc_drv_set_format(fmt);

            LOG("CLOCK: format switched to %s", use_24h ? "24h" : "12h");

            /* 触发全屏刷新 */
            trigger_full_refresh();
        }
    }
}

bool clock_mode_use_24h(void)
{
    return use_24h;
}

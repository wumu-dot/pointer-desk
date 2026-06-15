#include "settings_mode.h"
#include "gui.h"
#include "pointer_engine.h"
#include "rtc_drv.h"
#include "fs_mgr.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Sub-states                                                                 */
/* -------------------------------------------------------------------------- */
typedef enum {
    ST_MAIN_MENU,
    ST_BRIGHTNESS,
    ST_DATETIME,
    ST_TEMP_UNIT,
    ST_TIMER_DEFAULT,
    ST_FACTORY_RESET,
    ST_ABOUT
} settings_substate_t;

/* -------------------------------------------------------------------------- */
/*  Private static state                                                       */
/* -------------------------------------------------------------------------- */
static settings_substate_t substate;
static uint8_t  current_page;       /* 0 or 1 */
static uint8_t  cursor;             /* 0-2 */
static uint8_t  brightness;         /* saved value, 0-10 */
static uint8_t  brightness_value;   /* editing value, 0-10 */
static bool     temp_unit_fahrenheit; /* false = Celsius, true = Fahrenheit */
static uint32_t timer_default;      /* saved value, minutes */
static uint32_t timer_value;        /* editing value, minutes */
static bool     needs_render;       /* 脏标志: enter 或按键后置 true */

/* 日期时间编辑状态 */
static uint8_t  dt_cursor;          /* 编辑字段: 0=Year, 1=Month, 2=Day, 3=Hour, 4=Minute */
static uint16_t dt_year;
static uint8_t  dt_month;
static uint8_t  dt_day;
static uint8_t  dt_hour;
static uint8_t  dt_minute;

/* 月份每月的天数 */
static uint8_t days_in_month(uint8_t month, uint16_t year) {
    static const uint8_t dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t d = (month >= 1 && month <= 12) ? dim[month - 1] : 31;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        d = 29;
    }
    return d;
}

/* -------------------------------------------------------------------------- */
/*  Menu item labels                                                           */
/* -------------------------------------------------------------------------- */
static const char *menu_items_page0[] = {
    "Brightness",
    "Date & Time",
    "Temperature Unit"
};

static const char *menu_items_page1[] = {
    "Timer Default",
    "Factory Reset",
    "About"
};

/* -------------------------------------------------------------------------- */
/*  Public functions                                                           */
/* -------------------------------------------------------------------------- */

void settings_mode_init(void)
{
    /* Load saved brightness */
    if (fs_config_exists("brightness")) {
        if (!fs_config_load("brightness", &brightness, sizeof(brightness))) {
            brightness = LCD_BRIGHTNESS_DEFAULT / 10;
        }
    } else {
        brightness = LCD_BRIGHTNESS_DEFAULT / 10;
    }

    /* Load saved temperature unit */
    if (fs_config_exists("temp_unit")) {
        if (!fs_config_load("temp_unit", &temp_unit_fahrenheit,
                            sizeof(temp_unit_fahrenheit))) {
            temp_unit_fahrenheit = false;
        }
    } else {
        temp_unit_fahrenheit = false;
    }

    /* Load saved timer default */
    if (fs_config_exists("timer_default")) {
        if (!fs_config_load("timer_default", &timer_default,
                            sizeof(timer_default))) {
            timer_default = TIMER_DEFAULT_MINUTES;
        }
    } else {
        timer_default = TIMER_DEFAULT_MINUTES;
    }

    current_page = 0;
    cursor       = 0;

    LOG("settings_mode_init: brightness=%u, temp_f=%d, timer=%lu min",
        brightness, temp_unit_fahrenheit, (unsigned long)timer_default);
}

void settings_mode_enter(void)
{
    substate = ST_MAIN_MENU;
    needs_render = true;
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void settings_mode_exit(void)
{
    fs_config_save("brightness", &brightness, sizeof(brightness));
    fs_config_save("temp_unit", &temp_unit_fahrenheit,
                   sizeof(temp_unit_fahrenheit));
    fs_config_save("timer_default", &timer_default, sizeof(timer_default));

    LOG("settings_mode_exit: settings saved");
}

void settings_mode_update(void)
{
    pointer_set_page(current_page, 2);
}

void settings_mode_render(void)
{
    if (!needs_render) {
        return;
    }
    needs_render = false;

    switch (substate) {

    /* ================================================================== */
    /*  MAIN MENU  (2 pages x 3 items)                                    */
    /* ================================================================== */
    case ST_MAIN_MENU: {
        /* Title: "Settings 1/2" or "Settings 2/2" */
        {
            char title[24];
            snprintf(title, sizeof(title), "Settings %u/%u",
                     current_page + 1, 2);
            gui_draw_text_centered(LCD_WIDTH / 2, 8, title, 1,
                                   COLOR_WHITE, COLOR_BLACK);
        }

        /* Menu items */
        {
            const char **items = (current_page == 0)
                                     ? menu_items_page0
                                     : menu_items_page1;

            for (uint8_t i = 0; i < 3; i++) {
                uint16_t y = 40 + i * 30;

                if (i == cursor) {
                    /* Highlighted item */
                    st7735_fill_rect(0, y - 4, LCD_WIDTH, 22, COLOR_BLUE);
                    gui_draw_text_centered(LCD_WIDTH / 2, y, items[i], 1,
                                           COLOR_WHITE, COLOR_BLUE);
                } else {
                    /* Normal item */
                    gui_draw_text_centered(LCD_WIDTH / 2, y, items[i], 1,
                                           COLOR_GRAY, COLOR_BLACK);
                }
            }
        }

        /* Hint */
        gui_draw_text_centered(LCD_WIDTH / 2, 150,
                               "^v:nav  OK:enter  <:back",
                               0, COLOR_DARK_GRAY, COLOR_BLACK);
        break;
    }

    /* ================================================================== */
    /*  BRIGHTNESS EDITOR                                                  */
    /* ================================================================== */
    case ST_BRIGHTNESS: {
        /* Title */
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Brightness", 1,
                               COLOR_WHITE, COLOR_BLACK);

        /* Bar outline */
        st7735_draw_rect(20, 60, 88, 10, COLOR_GRAY);

        /* Bar fill (proportional to brightness_value / 10) */
        {
            uint16_t fill_w = (uint16_t)(88UL * brightness_value / 10);
            if (fill_w > 0) {
                st7735_fill_rect(20, 60, fill_w, 10, COLOR_CYAN);
            }
        }

        /* Percentage text */
        {
            char pct_str[16];
            snprintf(pct_str, sizeof(pct_str), "%u%%",
                     brightness_value * 10);
            gui_draw_text_centered(LCD_WIDTH / 2, 90, pct_str, 2,
                                   COLOR_WHITE, COLOR_BLACK);
        }

        /* Hint */
        gui_draw_text_centered(LCD_WIDTH / 2, 150,
                               "^v:adjust  OK:save  <:back",
                               0, COLOR_DARK_GRAY, COLOR_BLACK);
        break;
    }

    /* ================================================================== */
    /*  DATE & TIME (editable)                                              */
    /* ================================================================== */
    case ST_DATETIME: {
        gui_draw_text_centered(LCD_WIDTH / 2, 8, "Set Date & Time", 1,
                               COLOR_WHITE, COLOR_BLACK);

        /* 字段名 + 值, 光标字段高亮为YELLOW */
        const char *labels[] = {"Year", "Month", "Day", "Hour", "Min"};
        uint8_t vals[] = {0, 0, 0, 0, 0};
        char val_str[5][6];  /* max "2026\0" */
        snprintf(val_str[0], sizeof(val_str[0]), "%04u", dt_year);
        snprintf(val_str[1], sizeof(val_str[1]), "%02u", dt_month);
        snprintf(val_str[2], sizeof(val_str[2]), "%02u", dt_day);
        snprintf(val_str[3], sizeof(val_str[3]), "%02u", dt_hour);
        snprintf(val_str[4], sizeof(val_str[4]), "%02u", dt_minute);

        for (uint8_t i = 0; i < 5; i++) {
            uint16_t y = 30 + i * 24;
            uint16_t color = (i == dt_cursor) ? COLOR_YELLOW : COLOR_GRAY;

            gui_draw_text_aligned(y, labels[i], 0, color, COLOR_BLACK, GUI_ALIGN_LEFT);
            gui_draw_text_aligned(y, val_str[i], 1, color, COLOR_BLACK, GUI_ALIGN_RIGHT);

            if (i == dt_cursor) {
                gui_draw_text_aligned(y, "< >", 0, COLOR_YELLOW, COLOR_BLACK, GUI_ALIGN_CENTER);
            }
        }

        /* 第二行显示完整预览 */
        {
            char prev[32];
            snprintf(prev, sizeof(prev), "%04u-%02u-%02u %02u:%02u",
                     dt_year, dt_month, dt_day, dt_hour, dt_minute);
            gui_draw_text_centered(LCD_WIDTH / 2, 150, prev, 0,
                                   COLOR_CYAN, COLOR_BLACK);
        }
        break;
    }

    /* ================================================================== */
    /*  TEMPERATURE UNIT TOGGLE                                            */
    /* ================================================================== */
    case ST_TEMP_UNIT: {
        /* Title */
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Temperature Unit", 1,
                               COLOR_WHITE, COLOR_BLACK);

        /* Current unit (highlighted in YELLOW) */
        gui_draw_text_centered(LCD_WIDTH / 2, 70,
                               temp_unit_fahrenheit ? "\xB0""F" : "\xB0""C",
                               2, COLOR_YELLOW, COLOR_BLACK);

        /* Hint */
        gui_draw_text_centered(LCD_WIDTH / 2, 150,
                               "^v:switch  OK:save  <:back",
                               0, COLOR_DARK_GRAY, COLOR_BLACK);
        break;
    }

    /* ================================================================== */
    /*  TIMER DEFAULT EDITOR                                               */
    /* ================================================================== */
    case ST_TIMER_DEFAULT: {
        /* Title */
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Timer Default", 1,
                               COLOR_WHITE, COLOR_BLACK);

        /* Value in minutes */
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lu min",
                     (unsigned long)timer_value);
            gui_draw_text_centered(LCD_WIDTH / 2, 70, buf, 2,
                                   COLOR_CYAN, COLOR_BLACK);
        }

        /* Hint */
        gui_draw_text_centered(LCD_WIDTH / 2, 150,
                               "^v:adjust  OK:save  <:back",
                               0, COLOR_DARK_GRAY, COLOR_BLACK);
        break;
    }

    /* ================================================================== */
    /*  FACTORY RESET CONFIRMATION                                         */
    /* ================================================================== */
    case ST_FACTORY_RESET: {
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "Factory Reset?", 1,
                               COLOR_RED, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 60, "This will erase", 0,
                               COLOR_RED, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 75, "all settings!", 0,
                               COLOR_RED, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 105, "OK:confirm  <:cancel",
                               1, COLOR_YELLOW, COLOR_BLACK);
        break;
    }

    /* ================================================================== */
    /*  ABOUT                                                              */
    /* ================================================================== */
    case ST_ABOUT: {
        gui_draw_text_centered(LCD_WIDTH / 2, 30, "OV-Watch", 2,
                               COLOR_CYAN, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 65, "v1.0.0", 1,
                               COLOR_WHITE, COLOR_BLACK);
        gui_draw_text_centered(LCD_WIDTH / 2, 90,
                               "github.com/No-Chicken/ov-watch",
                               0, COLOR_DARK_GRAY, COLOR_BLACK);

        {
            char built_str[48];
            snprintf(built_str, sizeof(built_str), "Built: %s", __DATE__);
            gui_draw_text_centered(LCD_WIDTH / 2, 110, built_str, 0,
                                   COLOR_DARK_GRAY, COLOR_BLACK);
        }

        gui_draw_text_centered(LCD_WIDTH / 2, 150, "<: back",
                               0, COLOR_DARK_GRAY, COLOR_BLACK);
        break;
    }
    }
}

void settings_mode_handle_button(button_id_t btn, button_event_t event)
{
    /* Only process short presses */
    if (event != BTN_EVENT_SHORT_PRESS) {
        return;
    }

    needs_render = true;

    switch (substate) {

    /* ================================================================== */
    /*  MAIN MENU NAVIGATION                                               */
    /* ================================================================== */
    case ST_MAIN_MENU:
        switch (btn) {
        case BTN_UP:
            if (cursor > 0) {
                cursor--;
            }
            /* Fill menu area black and mark dirty */
            st7735_fill_rect(0, 36, LCD_WIDTH, 90, COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_DOWN:
            if (cursor < 2) {
                cursor++;
            }
            st7735_fill_rect(0, 36, LCD_WIDTH, 90, COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_LEFT:
            if (current_page > 0) {
                current_page--;
                cursor = 0;
                st7735_fill_screen(COLOR_BLACK);
                gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            }
            break;

        case BTN_RIGHT:
            if (current_page < 1) {
                current_page++;
                cursor = 0;
                st7735_fill_screen(COLOR_BLACK);
                gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            }
            break;

        case BTN_CENTER: {
            uint8_t item = current_page * 3 + cursor;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);

            switch (item) {
            case 0:
                substate         = ST_BRIGHTNESS;
                brightness_value = brightness;
                break;
            case 1:
                substate = ST_DATETIME;
                dt_cursor = 0;
                {
                    rtc_datetime_t dt = rtc_drv_get_datetime();
                    dt_year   = dt.date.year;
                    dt_month  = dt.date.month;
                    dt_day    = dt.date.day;
                    dt_hour   = dt.time.hours;
                    dt_minute = dt.time.minutes;
                }
                break;
            case 2:
                substate = ST_TEMP_UNIT;
                break;
            case 3:
                substate    = ST_TIMER_DEFAULT;
                timer_value = timer_default;
                break;
            case 4:
                substate = ST_FACTORY_RESET;
                break;
            case 5:
                substate = ST_ABOUT;
                break;
            default:
                break;
            }
            break;
        }

        default:
            break;
        }
        break;

    /* ================================================================== */
    /*  BRIGHTNESS EDITOR                                                  */
    /* ================================================================== */
    case ST_BRIGHTNESS:
        switch (btn) {
        case BTN_UP:
            if (brightness_value < 10) {
                brightness_value++;
                st7735_set_brightness(brightness_value * 10);
                st7735_fill_screen(COLOR_BLACK);
                gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            }
            break;

        case BTN_DOWN:
            if (brightness_value > 1) {
                brightness_value--;
                st7735_set_brightness(brightness_value * 10);
                st7735_fill_screen(COLOR_BLACK);
                gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            }
            break;

        case BTN_CENTER:
            brightness = brightness_value;
            fs_config_save("brightness", &brightness, sizeof(brightness));
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_LEFT:
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        default:
            break;
        }
        break;

    /* ================================================================== */
    /*  DATE & TIME EDITOR                                                  */
    /*  0=Year 1=Month 2=Day 3=Hour 4=Min, UP/+ DOWN/- LEFT/RIGHT nav    */
    /* ================================================================== */
    case ST_DATETIME:
        switch (btn) {
        case BTN_UP:
            switch (dt_cursor) {
            case 0: if (dt_year < 2099) dt_year++; break;
            case 1: if (dt_month < 12) dt_month++; else dt_month = 1; break;
            case 2: { uint8_t mx = days_in_month(dt_month, dt_year);
                      if (dt_day < mx) dt_day++; else dt_day = 1; } break;
            case 3: if (dt_hour < 23) dt_hour++; else dt_hour = 0; break;
            case 4: if (dt_minute < 59) dt_minute++; else dt_minute = 0; break;
            }
            needs_render = true;
            break;
        case BTN_DOWN:
            switch (dt_cursor) {
            case 0: if (dt_year > 2000) dt_year--; break;
            case 1: if (dt_month > 1) dt_month--; else dt_month = 12; break;
            case 2: { uint8_t mx = days_in_month(dt_month, dt_year);
                      if (dt_day > 1) dt_day--; else dt_day = mx; } break;
            case 3: if (dt_hour > 0) dt_hour--; else dt_hour = 23; break;
            case 4: if (dt_minute > 0) dt_minute--; else dt_minute = 59; break;
            }
            needs_render = true;
            break;
        case BTN_LEFT:
            if (dt_cursor == 0) {
                /* 从第一字段左键 = 返回主菜单, 不保存 */
                substate = ST_MAIN_MENU;
                needs_render = true;
            } else {
                dt_cursor--;
                needs_render = true;
            }
            break;
        case BTN_RIGHT:
            if (dt_cursor < 4) {
                dt_cursor++;
                needs_render = true;
            }
            break;
        case BTN_CENTER: {
            /* 写入 RTC */
            rtc_datetime_t dt;
            dt.time.hours   = dt_hour;
            dt.time.minutes = dt_minute;
            dt.time.seconds = 0;
            dt.date.year    = dt_year;
            dt.date.month   = dt_month;
            dt.date.day     = dt_day;
            dt.date.weekday = 1;  /* 简化: 设为周一, RTC 会自动跟踪 */
            rtc_drv_set_datetime(&dt);
            LOG("RTC set: %04u-%02u-%02u %02u:%02u",
                dt_year, dt_month, dt_day, dt_hour, dt_minute);
            substate = ST_MAIN_MENU;
            needs_render = true;
            break;
        }
        default:
            break;
        }
        break;

    /* ================================================================== */
    /*  TEMPERATURE UNIT TOGGLE                                            */
    /* ================================================================== */
    case ST_TEMP_UNIT:
        switch (btn) {
        case BTN_UP:
        case BTN_DOWN:
            temp_unit_fahrenheit = !temp_unit_fahrenheit;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_CENTER:
            fs_config_save("temp_unit", &temp_unit_fahrenheit,
                           sizeof(temp_unit_fahrenheit));
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_LEFT:
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        default:
            break;
        }
        break;

    /* ================================================================== */
    /*  TIMER DEFAULT EDITOR                                               */
    /* ================================================================== */
    case ST_TIMER_DEFAULT:
        switch (btn) {
        case BTN_UP:
            timer_value++;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_DOWN:
            if (timer_value > 1) {
                timer_value--;
            }
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_CENTER:
            timer_default = timer_value;
            fs_config_save("timer_default", &timer_default,
                           sizeof(timer_default));
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_LEFT:
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        default:
            break;
        }
        break;

    /* ================================================================== */
    /*  FACTORY RESET                                                      */
    /* ================================================================== */
    case ST_FACTORY_RESET:
        switch (btn) {
        case BTN_CENTER:
            fs_config_format();
            brightness = LCD_BRIGHTNESS_DEFAULT / 10;
            st7735_set_brightness(brightness * 10);
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        case BTN_LEFT:
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            break;

        default:
            break;
        }
        break;

    /* ================================================================== */
    /*  ABOUT (LEFT to go back)                                            */
    /* ================================================================== */
    case ST_ABOUT:
        if (btn == BTN_LEFT) {
            substate = ST_MAIN_MENU;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        }
        break;
    }
}

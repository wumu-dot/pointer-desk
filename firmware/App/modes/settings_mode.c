#include "settings_mode.h"
#include "mode_manager.h"
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
/*  Private static state                                                       */
/* -------------------------------------------------------------------------- */
static uint8_t  brightness;         /* saved value, 0-10 */
static bool     temp_unit_fahrenheit; /* false = Celsius, true = Fahrenheit */
static uint32_t timer_default;      /* saved value, minutes */
static bool     needs_render;       /* 脏标志: enter 或按键后置 true */

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

    LOG("settings_mode_init: brightness=%u, temp_f=%d, timer=%lu min",
        brightness, temp_unit_fahrenheit, (unsigned long)timer_default);
}

void settings_mode_enter(void)
{
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
    pointer_set_page(0, 1);
}

void settings_mode_render(void)
{
    if (!needs_render) {
        return;
    }
    needs_render = false;

    /* 单键只读模式: 显示系统信息 */
    gui_draw_text_centered(LCD_WIDTH / 2, 10, "Settings", 1, COLOR_WHITE, COLOR_BLACK);

    {
        rtc_datetime_t dt = rtc_drv_get_datetime();
        char buf[32];
        snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
                 dt.date.year, dt.date.month, dt.date.day,
                 dt.time.hours, dt.time.minutes, dt.time.seconds);
        gui_draw_text_centered(LCD_WIDTH / 2, 50, buf, 1, COLOR_CYAN, COLOR_BLACK);
    }

    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %u%%", (unsigned)brightness * 10);
        gui_draw_text_centered(LCD_WIDTH / 2, 80, buf, 0, COLOR_GRAY, COLOR_BLACK);
    }

    {
        char buf[32];
        snprintf(buf, sizeof(buf), "Timer: %lu min", (unsigned long)timer_default);
        gui_draw_text_centered(LCD_WIDTH / 2, 100, buf, 0, COLOR_GRAY, COLOR_BLACK);
    }

    gui_draw_text_centered(LCD_WIDTH / 2, 125,
                           temp_unit_fahrenheit ? "Unit: F" : "Unit: C",
                           0, COLOR_GRAY, COLOR_BLACK);

    gui_draw_text_centered(LCD_WIDTH / 2, 150, "HOLD: exit",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);
}

void settings_mode_handle_button(button_id_t btn, button_event_t event)
{
    /* 单键模式: 仅长按有效 → 退出设置菜单返回时钟 */
    if (event == BTN_EVENT_LONG_PRESS) {
        mode_manager_switch_to(MODE_CLOCK);
    }
}

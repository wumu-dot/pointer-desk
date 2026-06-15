#include "temp_mode.h"
#include "temp_sensor.h"
#include "gui.h"
#include "pointer_engine.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Private static state */
static bool fahrenheit = false;
static float current_temp = 0.0f;
static uint32_t last_read_ms = 0;

/* 渲染去重缓存 */
static struct {
    float temp;
    bool  fah;
    char  label[16];
} render_cache = { -999.0f, false, "" };

/* -------------------------------------------------------------------------- */
/*  Public functions                                                          */
/* -------------------------------------------------------------------------- */

void temp_mode_init(void)
{
    fahrenheit = false;
    memset(&render_cache, 0, sizeof(render_cache));
}

void temp_mode_enter(void)
{
    memset(&render_cache, 0, sizeof(render_cache));
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void temp_mode_exit(void)
{
    /* nothing to clean up */
}

void temp_mode_update(void)
{
    uint32_t now = HAL_GetTick();

    if ((now - last_read_ms) >= TEMP_READ_INTERVAL_MS) {
        last_read_ms = now;

        temp_data_t data = temp_sensor_read();
        current_temp = data.temperature;

        pointer_set_temperature(current_temp, fahrenheit);
    }
}

void temp_mode_render(void)
{
    float display_temp;

    if (fahrenheit) {
        display_temp = current_temp * 9.0f / 5.0f + 32.0f;
    } else {
        display_temp = current_temp;
    }

    /* 去重: 温度值和单位均无变化则跳过渲染 */
    const char *label = temp_sensor_get_label();
    {
        float diff = display_temp - render_cache.temp;
        if (diff < 0.0f) diff = -diff;
        if (diff < 0.05f && fahrenheit == render_cache.fah
            && (label == NULL || strcmp(label, render_cache.label) == 0)) {
            return;
        }
    }
    render_cache.temp = display_temp;
    render_cache.fah  = fahrenheit;
    if (label) {
        strncpy(render_cache.label, label, sizeof(render_cache.label) - 1);
    }

    /* --- Arc background (thin dark-gray arc behind tick marks) --- */
    gui_draw_arc(64, 70, 45, 30, 330, 3, COLOR_DARK_GRAY);

    /* --- 12 tick marks along the arc --- */
    gui_draw_tick_marks(64, 70, 45, 30, 330, 12, COLOR_GRAY);

    /* --- Meter: value + unit centred inside the arc --- */
    {
        char val_str[16];
        char unit_str[8];

        snprintf(val_str, sizeof(val_str), "%.1f", display_temp);
        snprintf(unit_str, sizeof(unit_str), "%s",
                 fahrenheit ? "°F" : "°C");

        gui_draw_meter(64, 75, val_str, unit_str, COLOR_WHITE, COLOR_CYAN);
    }

    /* --- Sensor label at bottom --- */
    {
        const char *label = temp_sensor_get_label();

        gui_draw_text_aligned(140,
                              label,
                              0,              /* font_id */
                              COLOR_DARK_GRAY,
                              COLOR_BLACK,
                              GUI_ALIGN_CENTER);
    }
}

void temp_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event != BTN_EVENT_SHORT_PRESS) {
        return;
    }

    if (btn == BTN_UP || btn == BTN_DOWN) {
        fahrenheit = !fahrenheit;
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    }
}

bool temp_mode_is_fahrenheit(void)
{
    return fahrenheit;
}

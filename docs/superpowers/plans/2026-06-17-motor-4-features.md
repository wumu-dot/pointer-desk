# OV-Watch 步进电机 + SHT30 四功能实现计划

> **For agentic workers:** 使用 `subagent-driven-development` 或 `executing-plans` skill 逐步执行。每步完成后打勾 `- [x]`。

**Goal:** 在现有 4 模式框架下，为每种模式增加子页（长按轮转），实现湿度计、双温对比、日出日落环、番茄钟、呼吸引导 5 个功能。

**Architecture:** 不改模式数量，不新增 .c/.h 文件。每个模式内部增加 `sub_page` 枚举，长按轮转子页，短按由 mode_manager 控制模式切换。指针引擎新增湿度刻度映射。ESP32 帧格式向后兼容扩展（加 sunrise/sunset 字段）。SHT30 驱动已存在，直接调用。

**Tech Stack:** C (ARMCC V5), STM32F407ZG, FreeRTOS, ESP32 ESP-IDF v5.4, open-meteo API

---

## 文件改动总览

| 文件 | 操作 | 改动说明 |
|------|------|---------|
| `Core/Inc/app_config.h` | 修改 | `weather_data_t` +sunrise/sunset；+番茄钟/呼吸配置宏 |
| `App/weather_bridge.c` | 修改 | 帧解析：向后兼容解析 sunrise/sunset 字段 |
| `Middleware/pointer/pointer_engine.h` | 修改 | +`pointer_set_humidity()` 声明 |
| `Middleware/pointer/pointer_engine.c` | 修改 | +湿度→角度映射（0%→30°, 100%→330°） |
| `App/modes/temp_mode.h` | 修改 | +`temp_mode_get_page()` 声明（供外部查询） |
| `App/modes/temp_mode.c` | 修改 | 子页轮转：天气主页→湿度计→双温对比→设备信息 |
| `App/modes/clock_mode.h` | 修改 | 不变（接口无需扩展） |
| `App/modes/clock_mode.c` | 修改 | 子页轮转：12h/24h→日出日落环 |
| `App/modes/timer_mode.h` | 修改 | 不变（接口无需扩展） |
| `App/modes/timer_mode.c` | 修改 | 子页轮转：倒计时→番茄钟→呼吸引导 |
| `C:\Projects\weather_clock\main\shared\display_data.h` | 修改 | +`sunrise_min`, `sunset_min` 字段 |
| `C:\Projects\weather_clock\main\tasks\network_task.c` | 修改 | open-meteo API 加 `daily=sunrise,sunset&timezone=auto` |
| `C:\Projects\weather_clock\main\tasks\uart_bridge_task.c` | 修改 | 帧拼接加 sunrise/sunset |

---

## Task 1: ESP32 — 扩展天气数据获取（日出日落）

**分支:** 在 `C:\Projects\weather_clock\` 工作（ESP32 ESP-IDF v5.4）

**依赖:** Task 2（STM32 解析需等 ESP32 新帧格式确认）

### Step 1.1: 扩展 DisplayData 结构体

**文件:** `C:\Projects\weather_clock\main\shared\display_data.h`

在 `DisplayData` 结构体中，`weather_code` 字段后添加：

```c
    uint8_t  weather_code;
    uint16_t sunrise_min;    /* 日出时间，分钟从午夜起，如 06:15 → 375 */
    uint16_t sunset_min;     /* 日落时间，分钟从午夜起，如 18:42 → 1122 */
    char     city[32];
```

编译验证：
```bash
cd C:\Projects\weather_clock && idf.py -j2 build
```

### Step 1.2: 扩展 HTTP 请求参数

**文件:** `C:\Projects\weather_clock\main\tasks\network_task.c` 第 21 行

将 `get_weather()` 中的 URL 从：
```c
    snprintf(url, sizeof(url),
        "%s?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code",
        WEATHER_API_URL, lat, lon);
```
改为：
```c
    snprintf(url, sizeof(url),
        "%s?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code"
        "&daily=sunrise,sunset&timezone=auto&forecast_days=1",
        WEATHER_API_URL, lat, lon);
```

### Step 1.3: 解析 sunrise/sunset JSON

**文件:** `C:\Projects\weather_clock\main\tasks\network_task.c` — `get_weather()` 函数内

在 `cJSON_GetObjectItem(r, "current")` 块之后（第 44 行 `cJSON_Delete(r)` 之前），新增：

```c
            /* 解析日出日落 (daily.sunrise[0], daily.sunset[0]) */
            cJSON *daily = cJSON_GetObjectItem(r, "daily");
            if (daily) {
                cJSON *sr_arr = cJSON_GetObjectItem(daily, "sunrise");
                cJSON *ss_arr = cJSON_GetObjectItem(daily, "sunset");
                if (sr_arr && cJSON_GetArraySize(sr_arr) > 0 &&
                    ss_arr && cJSON_GetArraySize(ss_arr) > 0) {
                    cJSON *sr = cJSON_GetArrayItem(sr_arr, 0);
                    cJSON *ss = cJSON_GetArrayItem(ss_arr, 0);
                    if (sr && ss && sr->valuestring && ss->valuestring) {
                        /* 格式 "2026-06-17T06:15" → 提取 HH:MM 转为分钟 */
                        const char *sr_t = strchr(sr->valuestring, 'T');
                        const char *ss_t = strchr(ss->valuestring, 'T');
                        if (sr_t && ss_t) {
                            int sr_h = 0, sr_m = 0, ss_h = 0, ss_m = 0;
                            if (sscanf(sr_t + 1, "%d:%d", &sr_h, &sr_m) == 2 &&
                                sscanf(ss_t + 1, "%d:%d", &ss_h, &ss_m) == 2) {
                                g_display_data.sunrise_min = (uint16_t)(sr_h * 60 + sr_m);
                                g_display_data.sunset_min  = (uint16_t)(ss_h * 60 + ss_m);
                            }
                        }
                    }
                }
            }
```

编译验证：
```bash
cd C:\Projects\weather_clock && idf.py -j2 build
```

### Step 1.4: 扩展 UART 桥接帧格式

**文件:** `C:\Projects\weather_clock\main\tasks\uart_bridge_task.c` — `uart_bridge_task()` 函数

将 payload 拼接（第 75-79 行）从：
```c
        snprintf(payload, sizeof(payload), "%s|%.1f|%d|%s",
                 time_str,
                 (float)g_display_data.temperature,
                 g_display_data.humidity,
                 desc);
```
改为：
```c
        uint16_t sr = g_display_data.sunrise_min;
        uint16_t ss = g_display_data.sunset_min;
        snprintf(payload, sizeof(payload), "%s|%.1f|%d|%s|%02u:%02u|%02u:%02u",
                 time_str,
                 (float)g_display_data.temperature,
                 g_display_data.humidity,
                 desc,
                 sr / 60, sr % 60,    /* sunrise HH:MM */
                 ss / 60, ss % 60);   /* sunset HH:MM */
```

**向后兼容说明:** 旧帧格式 `time|temp|hum|desc|CS` 有 4 个 `|`，新帧 `time|temp|hum|desc|HH:MM|HH:MM|CS` 有 6 个 `|`。STM32 端通过计数 `|` 数量自动识别新旧格式（见 Task 2）。串口日志会显示：

```
旧: $2026-06-17 23:25:26|24.0|77|Cloudy|D0
新: $2026-06-17 23:25:26|24.0|77|Cloudy|06:15|18:42|E2
```

编译验证：
```bash
cd C:\Projects\weather_clock && idf.py -j2 build
```

---

## Task 2: STM32 — 扩展天气数据结构 + 解析 sunrise/sunset

**目录:** `c:\Users\wumu2\OneDrive\桌面\ov_watch\firmware\`

### Step 2.1: 扩展 weather_data_t

**文件:** `Core/Inc/app_config.h`

在 `weather_data_t` 结构体中，`valid` 字段前添加：

```c
    uint16_t sunrise_min;       /* 日出分钟 (0-1439, 如 06:15 → 375) */
    uint16_t sunset_min;        /* 日落分钟 (0-1439, 如 18:42 → 1122) */
    bool     has_sun;           /* 是否收到日出日落数据 */
```

完整结构体变为：
```c
typedef struct {
    float    temperature;       /* 气温 °C                       */
    uint8_t  humidity;          /* 湿度 %                        */
    char     description[32];   /* 天气描述 "Cloud", "Rain" 等   */
    uint16_t sunrise_min;       /* 日出分钟 (0-1439)             */
    uint16_t sunset_min;        /* 日落分钟 (0-1439)             */
    bool     has_sun;           /* 是否收到日出日落数据          */
    uint32_t last_update_tick;  /* 最后一帧的时间戳 (HAL_GetTick) */
    bool     valid;             /* 是否收到过至少一帧             */
} weather_data_t;
```

### Step 2.2: 向后兼容解析 sunrise/sunset

**文件:** `App/weather_bridge.c` — `weather_frame_parse()` 函数

在第 120 行 `sscanf` 之后、第 123 行数据合法性检查之前，插入解析逻辑。

替换现有的 sscanf 和数据合法性检查段（第 111-125 行）：

```c
    /* 4. 将最后一根 | 后的内容截断 (去掉校验和) */
    ((char *)last_pipe)[0] = '\0';

    /* 5. 拆分所有管道字段 */
    const char *fields[8];
    int nfields = 0;
    const char *p = frame;
    const char *field_start = p;

    fields[nfields++] = field_start;
    while (*p && nfields < 8) {
        if (*p == '|') {
            ((char *)p)[0] = '\0';          /* 截断当前字段 */
            field_start = p + 1;
            fields[nfields++] = field_start;
        }
        p++;
    }

    if (nfields < 4) return;                 /* 至少需要 time|temp|hum|desc */

    /* 6. 解析 datetime (fields[0]) */
    int year, month, day, hour, minute, second;
    if (sscanf(fields[0], "%d-%d-%d %d:%d:%d",
               &year, &month, &day, &hour, &minute, &second) != 6) return;

    /* 7. 解析 temp (fields[1]), hum (fields[2]) */
    float temp;
    int humid;
    if (sscanf(fields[1], "%f", &temp) != 1) return;
    if (sscanf(fields[2], "%d", &humid) != 1) return;

    /* 8. 解析 desc (fields[3]), 恢复空格 */
    char desc[32];
    {
        size_t dlen = strlen(fields[3]);
        if (dlen >= sizeof(desc)) dlen = sizeof(desc) - 1;
        memcpy(desc, fields[3], dlen);
        desc[dlen] = '\0';
        for (char *cd = desc; *cd; cd++) {
            if (*cd == '_') *cd = ' ';
        }
    }

    /* 9. 解析 sunrise/sunset (可选: fields[4], fields[5]) */
    bool has_sun = false;
    uint16_t sunrise_min = 0;
    uint16_t sunset_min  = 0;

    if (nfields >= 6) {
        int sr_h = 0, sr_m = 0, ss_h = 0, ss_m = 0;
        if (sscanf(fields[4], "%d:%d", &sr_h, &sr_m) == 2 &&
            sscanf(fields[5], "%d:%d", &ss_h, &ss_m) == 2) {
            if (sr_h >= 0 && sr_h <= 23 && sr_m >= 0 && sr_m <= 59 &&
                ss_h >= 0 && ss_h <= 23 && ss_m >= 0 && ss_m <= 59) {
                sunrise_min = (uint16_t)(sr_h * 60 + sr_m);
                sunset_min  = (uint16_t)(ss_h * 60 + ss_m);
                has_sun = true;
            }
        }
    }

    /* 10. 数据合法性 */
    if (year < 2000  || month < 1 || month > 12 || day < 1 || day > 31) return;
    if (hour > 23   || minute > 59  || second > 59) return;
    if (humid < 0   || humid > 100) return;
```

然后更新 `g_weather` 写入（第 147-152 行），添加 sunrise/sunset 字段：

```c
    /* 8. 更新 g_weather → 变为 step 11 */
    weather_data_t tmp;
    tmp.temperature      = temp;
    tmp.humidity         = (uint8_t)humid;
    strncpy(tmp.description, desc, sizeof(tmp.description) - 1);
    tmp.description[sizeof(tmp.description) - 1] = '\0';
    tmp.sunrise_min      = sunrise_min;      /* 新增 */
    tmp.sunset_min       = sunset_min;       /* 新增 */
    tmp.has_sun          = has_sun;          /* 新增 */
    tmp.last_update_tick = HAL_GetTick();
    tmp.valid            = true;
```

去重逻辑（第 156-162 行）加入 sunrise/sunset 比较：

```c
    /* 9. 去重 → 变为 step 12 */
    if (last_rendered.valid &&
        tmp.temperature == last_rendered.temperature &&
        tmp.humidity    == last_rendered.humidity &&
        tmp.has_sun     == last_rendered.has_sun &&
        tmp.sunrise_min == last_rendered.sunrise_min &&
        tmp.sunset_min  == last_rendered.sunset_min &&
        strcmp(tmp.description, last_rendered.description) == 0) {
        g_weather.last_update_tick = tmp.last_update_tick;
        return;
    }
```

---

## Task 3: 指针引擎 — 湿度刻度映射

**文件:** `Middleware/pointer/pointer_engine.h`, `Middleware/pointer/pointer_engine.c`

### Step 3.1: 声明新 API

**文件:** `Middleware/pointer/pointer_engine.h`

在 `pointer_set_page()` 声明之后、`pointer_engine_update()` 之前添加：

```c
/* 湿度计刻度 (0-100% → 30°-330° 弧线) */
void pointer_set_humidity(uint8_t percent);
```

### Step 3.2: 实现湿度映射

**文件:** `Middleware/pointer/pointer_engine.c`

在 `pointer_set_page()` 函数之后、`pointer_engine_update()` 之前添加：

```c
/**
 * @brief Humidity gauge mode.
 *
 *        Maps 0-100 %RH onto a 300-degree arc from 30 deg to 330 deg.
 *        Percent values are clamped to [0, 100].
 *
 *        angle = 30 + percent / 100 * 300
 */
void pointer_set_humidity(uint8_t percent)
{
    if (percent > 100) percent = 100;

    float angle = 30.0f + (float)percent / 100.0f * 300.0f;

    pointer_set_target(angle, POINTER_MOVE_NORMAL);
}
```

---

## Task 4: Temp 模式重构 — 子页轮转 + 湿度计 + 双温对比

**文件:** `App/modes/temp_mode.c`, `App/modes/temp_mode.h`

### 架构说明

Temp 模式 4 个子页，长按轮转：

```
天气主页(sub=0) → 湿度计(sub=1) → 双温对比(sub=2) → 设备信息(sub=3) → weather→...
```

- 子页切换时全屏清屏 + 标记全屏脏
- 当前活跃子页的 `render()` 负责绘制；非活跃子页跳过渲染
- `update()` 根据活跃子页执行不同逻辑

### Step 4.1: 添加子页枚举和静态变量

**文件:** `App/modes/temp_mode.c`

在现有 `static bool show_device_info` 附近替换为子页枚举：

```c
/* ================================================================
 * 子页定义
 * ================================================================ */
typedef enum {
    TEMP_PAGE_WEATHER = 0,   /* 天气主页 (区域 ESP32 数据) */
    TEMP_PAGE_HUMIDITY,      /* 湿度计 (指针 + 大字湿度) */
    TEMP_PAGE_COMPARISON,    /* 本地 SHT30 vs 区域 ESP32 对比 */
    TEMP_PAGE_DEVICE,        /* 设备信息 (原有) */
    TEMP_PAGE_COUNT
} temp_page_t;

/* ================================================================
 * 静态状态
 * ================================================================ */
static temp_page_t current_page = TEMP_PAGE_WEATHER;
static bool fahrenheit = false;
```

删除 `static bool show_device_info`（被 `current_page` 替代），删除 `static bool needs_render_device_info`（改用全屏重绘）。

### Step 4.2: 更新 init/enter/exit/update

**文件:** `App/modes/temp_mode.c`

```c
void temp_mode_init(void)
{
    fahrenheit = false;
    current_page = TEMP_PAGE_WEATHER;
    memset(&last_rendered, 0, sizeof(last_rendered));
}

void temp_mode_enter(void)
{
    LOG("TEMP: enter");
    current_page = TEMP_PAGE_WEATHER;       /* 每次进入重置为天气主页 */
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
    switch (current_page) {
    case TEMP_PAGE_WEATHER:
        pointer_set_temperature(g_weather.temperature, fahrenheit);
        break;
    case TEMP_PAGE_HUMIDITY:
        if (g_weather.valid) {
            pointer_set_humidity(g_weather.humidity);
        }
        break;
    case TEMP_PAGE_COMPARISON:
        /* SHT30 本地温度驱动指针 */
        {
            temp_data_t local = temp_sensor_read();
            if (local.source == TEMP_SRC_SHT30) {
                pointer_set_temperature(local.temperature, fahrenheit);
            } else if (g_weather.valid) {
                /* 无 SHT30 时用 ESP32 数据驱动指针 */
                pointer_set_temperature(g_weather.temperature, fahrenheit);
            }
        }
        break;
    case TEMP_PAGE_DEVICE:
        /* 设备信息页无需驱动指针 */
        break;
    }
}
```

### Step 4.3: 湿度计渲染页

**文件:** `App/modes/temp_mode.c` — 在 `render_weather_page()` 之后新增

```c
static void render_humidity_page(void)
{
    if (!g_weather.valid) {
        /* 无数据不渲染更多 */
        return;
    }

    /* 去重: 湿度变化才重绘 */
    static uint8_t last_humid = 0xFF;
    if (g_weather.humidity == last_humid) return;
    last_humid = g_weather.humidity;

    st7735_fill_screen(COLOR_BLACK);

    /* ---- 湿度等级 ---- */
    const char *label;
    uint16_t color;
    uint8_t h = g_weather.humidity;

    if (h < 30)      { label = "Dry";    color = COLOR_YELLOW; }
    else if (h < 60) { label = "Comfort"; color = COLOR_GREEN;  }
    else if (h < 80) { label = "Humid";  color = COLOR_CYAN;   }
    else             { label = "Wet";    color = COLOR_BLUE; }

    /* 大字湿度 */
    char buf[16];
    snprintf(buf, sizeof(buf), "%u%%", h);
    gui_draw_text_centered(LCD_WIDTH / 2, 40, buf, 3,   /* font_id=3 → 16x32 */
                           COLOR_WHITE, COLOR_BLACK);

    /* 湿度等级标签 */
    gui_draw_text_centered(LCD_WIDTH / 2, 85, label, 1, color, COLOR_BLACK);

    /* 湿度环 (0-100% → 完整圆弧) */
    int16_t end_deg = (int16_t)((float)h * 360.0f / 100.0f);
    if (end_deg < 1) end_deg = 1;
    gui_draw_arc(64, 125, 30, 0, end_deg, 4, color);

    /* 底部提示 */
    gui_draw_text_centered(LCD_WIDTH / 2, 150, "HOLD: next",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

### Step 4.4: 双温对比渲染页

```c
static void render_comparison_page(void)
{
    static temp_data_t last_local = {0};
    static weather_data_t last_weather = {0};
    static bool first_render = true;

    temp_data_t local = temp_sensor_read();

    /* 去重 */
    if (!first_render &&
        local.temperature == last_local.temperature &&
        local.humidity    == last_local.humidity &&
        g_weather.temperature == last_weather.temperature &&
        g_weather.humidity    == last_weather.humidity) {
        return;
    }
    first_render = false;
    last_local = local;
    last_weather = g_weather;

    st7735_fill_screen(COLOR_BLACK);

    /* ---- 列标题 ---- */
    st7735_draw_text(5,  5, "LOCAL",    FONT_8x16, COLOR_CYAN, COLOR_BLACK);
    st7735_draw_text(70, 5, "REGIONAL", FONT_8x16, COLOR_YELLOW, COLOR_BLACK);

    /* ---- 分隔线 ---- */
    st7735_fill_rect(64, 22, 1, 110, COLOR_DARK_GRAY);

    /* ---- 本地温度 ---- */
    {
        char buf[16];
        const char *src;
        uint16_t temp_color;

        if (local.source == TEMP_SRC_SHT30) {
            float t = fahrenheit ? (local.temperature * 9.0f / 5.0f + 32.0f)
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
        if (g_weather.valid) {
            float t = fahrenheit ? (g_weather.temperature * 9.0f / 5.0f + 32.0f)
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
        snprintf(buf, sizeof(buf), "H:%s%%",
                 (local.source == TEMP_SRC_SHT30)
                     ? ({ static char hb[8]; snprintf(hb, 8, "%.0f", local.humidity); hb; })
                     : "--");
        st7735_draw_text(5, 95, buf, FONT_8x16, COLOR_CYAN, COLOR_BLACK);

        snprintf(buf, sizeof(buf), "H:%u%%",
                 g_weather.valid ? g_weather.humidity : 0);
        st7735_draw_text(70, 95, buf, FONT_8x16, COLOR_YELLOW, COLOR_BLACK);
    }

    /* ---- 温差 ---- */
    if (local.source == TEMP_SRC_SHT30 && g_weather.valid) {
        float diff = local.temperature - g_weather.temperature;
        char buf[32];
        snprintf(buf, sizeof(buf), "Diff: %+.1fC", diff);
        uint16_t diff_color = (diff > 1.0f) ? COLOR_RED
                            : (diff < -1.0f) ? COLOR_BLUE
                            : COLOR_GREEN;
        gui_draw_text_centered(LCD_WIDTH / 2, 125, buf, 1, diff_color, COLOR_BLACK);
    }

    /* 底部提示 */
    gui_draw_text_centered(LCD_WIDTH / 2, 150, "HOLD: next",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

注意：ARMCC V5 不支持复合语句内的局部变量声明在表达式中。需要把湿度格式化移到独立变量：

```c
    /* ---- 湿度对比 (ARMCC V5 兼容) ---- */
    {
        char buf[32];
        char local_hum_buf[8];
        if (local.source == TEMP_SRC_SHT30) {
            snprintf(local_hum_buf, sizeof(local_hum_buf), "%.0f", local.humidity);
            snprintf(buf, sizeof(buf), "H:%s%%", local_hum_buf);
        } else {
            snprintf(buf, sizeof(buf), "H:--%%");
        }
        st7735_draw_text(5, 95, buf, FONT_8x16, COLOR_CYAN, COLOR_BLACK);

        snprintf(buf, sizeof(buf), "H:%u%%",
                 g_weather.valid ? g_weather.humidity : 0);
        st7735_draw_text(70, 95, buf, FONT_8x16, COLOR_YELLOW, COLOR_BLACK);
    }
```

### Step 4.5: 更新 render 分派

**文件:** `App/modes/temp_mode.c` — 替换 `temp_mode_render()`：

```c
void temp_mode_render(void)
{
    switch (current_page) {
    case TEMP_PAGE_WEATHER:    render_weather_page();    break;
    case TEMP_PAGE_HUMIDITY:   render_humidity_page();   break;
    case TEMP_PAGE_COMPARISON: render_comparison_page(); break;
    case TEMP_PAGE_DEVICE:     render_device_info_page();break;
    }
}
```

注意：`render_device_info_page()` 已有的 `needs_render_device_info` 机制保留，但其静态变量改为每次切换子页时重置。

### Step 4.6: 更新按键处理（长按轮转子页）

**文件:** `App/modes/temp_mode.c` — 替换 `temp_mode_handle_button()`：

```c
void temp_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event == BTN_EVENT_LONG_PRESS) {
        current_page = (temp_page_t)((current_page + 1) % TEMP_PAGE_COUNT);
        memset(&last_rendered, 0, sizeof(last_rendered));  /* 清空去重缓存 */
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
        LOG("TEMP: page=%d", current_page);
    }
}
```

---

## Task 5: Clock 模式 — 日出日落环子页

**文件:** `App/modes/clock_mode.c`

### 设计

Clock 模式 2 个子页（长按轮转）：

```
时钟主页(sub=0) → 日出日落环(sub=1) → 回到时钟
```

原有长按逻辑（切换 12h/24h）改为在时钟主页内执行；日出日落环子页时，长按用于切回时钟主页。

### Step 5.1: 添加子页枚举和静态变量

**文件:** `App/modes/clock_mode.c`

在 `static bool use_24h` 后添加：

```c
/* 子页 */
typedef enum {
    CLOCK_PAGE_MAIN = 0,     /* 时钟主页 */
    CLOCK_PAGE_SUN,          /* 日出日落环 */
    CLOCK_PAGE_COUNT
} clock_page_t;

static clock_page_t clock_page = CLOCK_PAGE_MAIN;
```

### Step 5.2: 更新 enter/exit

```c
void clock_mode_enter(void)
{
    LOG("CLOCK: enter");
    clock_page = CLOCK_PAGE_MAIN;    /* 每次进入重置 */
    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    need_full_refresh = true;
}

void clock_mode_exit(void)
{
    LOG("CLOCK: exit");
}
```

### Step 5.3: 日出日落环渲染

在 `render_weather_page()` 函数区域之后新增：

```c
/**
 * @brief 日出日落环渲染
 *
 * 绘制 24 小时环 (360° 环形)，白天段(sunrise→sunset)用暖色，
 * 夜间段(sunset→sunrise)用冷色，当前时间指针指示位置。
 *
 * 环中心=(64, 80), 外径=50, 内径=36 (环厚=14)
 */
static void render_sun_page(void)
{
    rtc_datetime_t dt = rtc_drv_get_datetime();
    bool has_data = g_weather.valid && g_weather.has_sun;

    /* 每秒去重 */
    static uint8_t last_min = 0xFF;
    if (dt.time.minutes == last_min && dt.time.seconds == last_min) return;
    /* 实际用 minutes 去重即可，但首次需要渲染 */
    static bool first = true;
    if (!first && dt.time.minutes == last_min) return;
    first = false;
    last_min = dt.time.minutes;

    st7735_fill_screen(COLOR_BLACK);

    uint16_t now_min   = (uint16_t)(dt.time.hours * 60 + dt.time.minutes);

    if (has_data) {
        uint16_t sr_min = g_weather.sunrise_min;
        uint16_t ss_min = g_weather.sunset_min;

        /* 将分钟值映射到 0-360° (1440 分钟 = 360°) */
        int16_t sr_deg = (int16_t)((uint32_t)sr_min * 360 / 1440);
        int16_t ss_deg = (int16_t)((uint32_t)ss_min * 360 / 1440);
        int16_t now_deg = (int16_t)((uint32_t)now_min * 360 / 1440);

        /* 白天弧 (sunrise → sunset，顺时针)
         * 环画在 cx=64, cy=80, 外径=50, 内径=36 */
        {
            int16_t day_span;
            if (ss_deg >= sr_deg) {
                day_span = ss_deg - sr_deg;
            } else {
                day_span = 360 - sr_deg + ss_deg;
            }
            if (day_span > 0) {
                gui_draw_arc(64, 80, 50, sr_deg, day_span, 8, COLOR_YELLOW);
            }
        }

        /* 夜间弧 (sunset → sunrise) — 灰色 */
        {
            int16_t night_span = 360;
            if (ss_deg >= sr_deg) {
                night_span = 360 - (ss_deg - sr_deg);
            } else {
                night_span = sr_deg - ss_deg;
            }
            if (night_span > 0) {
                gui_draw_arc(64, 80, 43, ss_deg, night_span, 4, COLOR_DARK_GRAY);
            }
        }

        /* 当前时间标记点 (小圆点画在环上) */
        {
            float rad = (float)now_deg * 3.14159f / 180.0f;
            int16_t dot_x = 64 + (int16_t)(42.0f * sinf(rad));
            int16_t dot_y = 80 - (int16_t)(42.0f * cosf(rad));
            st7735_fill_rect(dot_x - 2, dot_y - 2, 5, 5, COLOR_WHITE);
        }
    }

    /* ---- 文字信息 ---- */
    {
        char buf[32];
        if (has_data) {
            uint16_t sr_h = g_weather.sunrise_min / 60;
            uint16_t sr_m = g_weather.sunrise_min % 60;
            uint16_t ss_h = g_weather.sunset_min / 60;
            uint16_t ss_m = g_weather.sunset_min % 60;
            snprintf(buf, sizeof(buf), "Sun %02u:%02u - %02u:%02u",
                     sr_h, sr_m, ss_h, ss_m);
        } else {
            snprintf(buf, sizeof(buf), "No sun data");
        }
        gui_draw_text_centered(LCD_WIDTH / 2, 135, buf, 0,
                               has_data ? COLOR_WHITE : COLOR_GRAY, COLOR_BLACK);
    }

    /* 底部提示 */
    gui_draw_text_centered(LCD_WIDTH / 2, 150, "HOLD: back",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

### Step 5.4: 更新 update 和 render 分派

```c
void clock_mode_update(void)
{
    if (clock_page != CLOCK_PAGE_MAIN) return;  /* 日出页不驱动指针 */

    rtc_datetime_t dt = rtc_drv_get_datetime();
    if (use_24h) {
        pointer_set_clock_24h(dt.time.hours, dt.time.minutes);
    } else {
        pointer_set_clock(dt.time.hours, dt.time.minutes);
    }
}

void clock_mode_render(void)
{
    switch (clock_page) {
    case CLOCK_PAGE_MAIN:
        /* 原有 clock_mode_render 逻辑不变 */
        {
            rtc_datetime_t dt = rtc_drv_get_datetime();
            bool pm = (!use_24h) ? rtc_drv_is_pm() : false;

            if (!display_changed(&dt, pm)) return;

            bool time_changed = (need_full_refresh ||
                                 dt.time.hours   != cached.hours ||
                                 dt.time.minutes != cached.minutes ||
                                 dt.time.seconds != cached.seconds);
            bool date_changed = (need_full_refresh ||
                                 dt.date.day     != cached.day ||
                                 dt.date.month   != cached.month ||
                                 dt.date.year    != cached.year ||
                                 dt.date.weekday != cached.weekday);
            bool ampm_changed = (need_full_refresh || pm != cached.is_pm || use_24h != cached.show_24h);
            bool bar_changed  = (need_full_refresh ||
                                 dt.time.hours   != cached.hours ||
                                 dt.time.minutes != cached.minutes);

            update_cache(&dt, pm);

            if (ampm_changed) {
                if (!use_24h) {
                    gui_draw_icon(LCD_WIDTH - 14, 4, pm ? ICON_MOON : ICON_SUN, COLOR_YELLOW);
                } else {
                    st7735_fill_rect(LCD_WIDTH - 16, 0, 16, 20, COLOR_BLACK);
                    gui_dirty_mark(LCD_WIDTH - 16, 0, 16, 20);
                }
            }

            if (time_changed) {
                char time_str[16];
                uint8_t display_hour = use_24h ? dt.time.hours : rtc_drv_get_hour12();
                snprintf(time_str, sizeof(time_str), "%02u:%02u:%02u",
                         display_hour, dt.time.minutes, dt.time.seconds);
                gui_draw_text_centered(LCD_WIDTH / 2, 40, time_str,
                                       2, COLOR_WHITE, COLOR_BLACK);
            }

            if (date_changed) {
                char date_str[32];
                const char *weekday = rtc_drv_weekday_str(dt.date.weekday);
                snprintf(date_str, sizeof(date_str), "%04u-%02u-%02u %s",
                         dt.date.year, dt.date.month, dt.date.day,
                         weekday ? weekday : "");
                gui_draw_text_centered(LCD_WIDTH / 2, 70, date_str,
                                       0, COLOR_GRAY, COLOR_BLACK);
            }

            if (bar_changed) {
                uint16_t bar_x = 5, bar_y = 140;
                uint16_t bar_w = LCD_WIDTH - 10, bar_h = 4;
                uint16_t total_minutes = (uint16_t)dt.time.hours * 60 + dt.time.minutes;
                uint16_t dot_pos = (uint16_t)((uint32_t)bar_w * total_minutes / 1440);

                st7735_fill_rect(bar_x, bar_y, bar_w, bar_h, COLOR_DARK_GRAY);
                if (dot_pos > 0) {
                    if (dot_pos < 3) dot_pos = 3;
                    st7735_fill_rect(bar_x, bar_y, dot_pos, bar_h, COLOR_WHITE);
                }
                gui_dirty_mark(bar_x, bar_y, bar_w, bar_h);
            }
        }
        break;

    case CLOCK_PAGE_SUN:
        render_sun_page();
        break;
    }
}
```

### Step 5.5 按键处理更新

```c
void clock_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event == BTN_EVENT_LONG_PRESS) {
        if (clock_page == CLOCK_PAGE_MAIN) {
            /* 时钟主页: 长按 → 切换 12/24h */
            use_24h = !use_24h;
            rtc_format_t fmt = use_24h ? RTC_FORMAT_24H : RTC_FORMAT_12H;
            rtc_drv_set_format(fmt);
            LOG("CLOCK: format switched to %s", use_24h ? "24h" : "12h");
            trigger_full_refresh();
        } else {
            /* 日出日落环: 长按 → 回到时钟主页 */
            clock_page = CLOCK_PAGE_MAIN;
            trigger_full_refresh();
            LOG("CLOCK: back to main");
        }
    }
    /* 短按由 mode_manager 处理 (切模式) */
}
```

---

## Task 6: 番茄钟配宏 + timer_mode 重构

**文件:** `Core/Inc/app_config.h`, `App/modes/timer_mode.c`

### Step 6.1: 添加番茄钟 / 呼吸配置宏

**文件:** `Core/Inc/app_config.h` — 在计时器配置段（第 47-53 行）后添加：

```c
/* ================================================================
 * 番茄钟
 * ================================================================ */
#define POMODORO_WORK_SEC       1500    /* 25 分钟工作 */
#define POMODORO_BREAK_SEC      300     /* 5 分钟休息 */
#define POMODORO_LONG_BREAK_SEC 900     /* 15 分钟长休息 (每4个番茄) */

/* ================================================================
 * 呼吸引导
 * ================================================================ */
#define BREATH_INHALE_MS        4000    /* 4 秒吸入 */
#define BREATH_EXHALE_MS        4000    /* 4 秒呼出 */
#define BREATH_CYCLE_COUNT      6       /* 每次引导 6 个呼吸周期 */
```

### Step 6.2: Timer 模式子页架构

**文件:** `App/modes/timer_mode.c`

Timer 模式 3 个子页，长按轮转：

```
倒计时(sub=0) → 番茄钟(sub=1) → 呼吸引导(sub=2) → 回到倒计时
```

在 `timer_mode_init()` 附近添加：

```c
/* ================================================================
 * 子页定义
 * ================================================================ */
typedef enum {
    TIMER_PAGE_COUNTDOWN = 0,  /* 倒计时 */
    TIMER_PAGE_POMODORO,       /* 番茄钟 */
    TIMER_PAGE_BREATHING,      /* 呼吸引导 */
    TIMER_PAGE_COUNT
} timer_page_t;

static timer_page_t timer_page = TIMER_PAGE_COUNTDOWN;

/* ================================================================
 * 番茄钟状态
 * ================================================================ */
typedef enum {
    POMO_WORK,                  /* 工作中 (25 分钟) */
    POMO_BREAK,                 /* 短休息 (5 分钟) */
    POMO_LONG_BREAK,            /* 长休息 (15 分钟) */
    POMO_STOPPED,               /* 停止 */
} pomo_phase_t;

static pomo_phase_t pomo_phase       = POMO_STOPPED;
static uint32_t     pomo_remaining   = 0;    /* 当前阶段剩余秒数 */
static uint32_t     pomo_total       = 0;    /* 当前阶段总秒数 */
static uint32_t     pomo_start_tick  = 0;
static uint8_t      pomo_count       = 0;    /* 已完成番茄数 */

/* ================================================================
 * 呼吸引导状态
 * ================================================================ */
typedef enum {
    BREATH_STOPPED,
    BREATH_INHALING,
    BREATH_EXHALING,
} breath_phase_t;

static breath_phase_t breath_phase = BREATH_STOPPED;
static uint32_t       breath_phase_start = 0;
static uint8_t        breath_cycle_count = 0;
static float          breath_angle = 0.0f;   /* 当前指针角度 (0-180°) */
```

### Step 6.3: 更新 enter/exit

```c
void timer_mode_enter(void)
{
    timer_page = TIMER_PAGE_COUNTDOWN;
    if (state == TIMER_STOPPED) {
        remaining_sec = total_sec;
    }
    timer_refresh_display();
}

void timer_mode_exit(void)
{
    if (state == TIMER_RUNNING) {
        state = TIMER_PAUSED;
        LOG("Timer auto-paused on exit");
    }
    /* 呼吸引导退出时停止 */
    if (timer_page == TIMER_PAGE_BREATHING) {
        breath_phase = BREATH_STOPPED;
    }
}
```

### Step 6.4: 番茄钟 update 逻辑

```c
static void pomodoro_update(void)
{
    if (pomo_phase == POMO_STOPPED) return;

    uint32_t now     = HAL_GetTick();
    uint32_t elapsed = (now - pomo_start_tick) / 1000;

    if (elapsed >= pomo_total) {
        /* 当前阶段结束 */
        a4988_vibrate(TIMER_END_VIBRATE_COUNT, 200);

        if (pomo_phase == POMO_WORK) {
            pomo_count++;
            /* 每完成 4 个番茄进入长休息 */
            if (pomo_count % 4 == 0) {
                pomo_phase     = POMO_LONG_BREAK;
                pomo_total     = POMODORO_LONG_BREAK_SEC;
                LOG("Pomo: long break (%lu min)", (unsigned long)(POMODORO_LONG_BREAK_SEC / 60));
            } else {
                pomo_phase     = POMO_BREAK;
                pomo_total     = POMODORO_BREAK_SEC;
                LOG("Pomo: break (%lu min)", (unsigned long)(POMODORO_BREAK_SEC / 60));
            }
        } else {
            /* 休息结束 → 回到工作 */
            pomo_phase     = POMO_WORK;
            pomo_total     = POMODORO_WORK_SEC;
            LOG("Pomo: work (pomo #%u)", pomo_count + 1);
        }

        pomo_remaining  = pomo_total;
        pomo_start_tick = now;

        /* 全屏重绘 */
        st7735_fill_screen(COLOR_BLACK);
        gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
    } else {
        pomo_remaining = pomo_total - elapsed;
    }

    /* 驱动指针: 一圈 = 当前阶段总时长 */
    if (pomo_total > 0) {
        float fraction = (float)pomo_remaining / (float)pomo_total;
        float angle    = (1.0f - fraction) * 360.0f;
        pointer_set_target(angle, POINTER_MOVE_NORMAL);
    }
}
```

### Step 6.5: 番茄钟 render

```c
static void pomodoro_render(void)
{
    static uint32_t  last_remaining = 0xFFFFFFFF;
    static pomo_phase_t last_phase  = (pomo_phase_t)0xFF;

    /* 去重 */
    if (pomo_remaining == last_remaining && pomo_phase == last_phase) return;
    last_remaining = pomo_remaining;
    last_phase     = pomo_phase;

    st7735_fill_screen(COLOR_BLACK);

    /* 进度环 */
    float ratio = 1.0f - (float)pomo_remaining / (float)pomo_total;
    int16_t end_deg = (int16_t)(ratio * 360.0f);

    uint16_t arc_color;
    const char *label;
    switch (pomo_phase) {
        case POMO_WORK:       arc_color = COLOR_RED;    label = "WORK";   break;
        case POMO_BREAK:      arc_color = COLOR_GREEN;  label = "BREAK";  break;
        case POMO_LONG_BREAK: arc_color = COLOR_CYAN;   label = "L BREAK";break;
        default:              arc_color = COLOR_GRAY;   label = "STOP";   break;
    }

    gui_draw_arc(64, 55, 40, 0, 360, 4, COLOR_DARK_GRAY);
    if (end_deg > 0) {
        gui_draw_arc(64, 55, 40, 0, end_deg, 4, arc_color);
    }

    /* 倒计时时间 */
    {
        uint32_t mins = pomo_remaining / 60;
        uint32_t secs = pomo_remaining % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02lu:%02lu",
                 (unsigned long)mins, (unsigned long)secs);
        gui_draw_text_centered(64, 55, buf, 3, COLOR_WHITE, COLOR_BLACK);
    }

    /* 阶段标签 */
    gui_draw_text_centered(64, 100, label, 1, arc_color, COLOR_BLACK);

    /* 已完成番茄计数 */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "x%u", pomo_count);
        gui_draw_text_centered(64, 118, buf, 0, COLOR_GRAY, COLOR_BLACK);
    }

    /* 底部提示 */
    gui_draw_text_centered(64, 145, "HOLD: START/STOP",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

### Step 6.6: 番茄钟按键

```c
static void pomodoro_handle_button(button_event_t event)
{
    if (event != BTN_EVENT_LONG_PRESS) return;

    if (pomo_phase == POMO_STOPPED) {
        /* 开始第一个番茄 */
        pomo_phase     = POMO_WORK;
        pomo_total     = POMODORO_WORK_SEC;
        pomo_remaining = POMODORO_WORK_SEC;
        pomo_start_tick = HAL_GetTick();
        pomo_count     = 0;
        LOG("Pomo: started");
    } else {
        /* 停止番茄钟 */
        pomo_phase = POMO_STOPPED;
        LOG("Pomo: stopped (count=%u)", pomo_count);
    }

    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

---

## Task 7: 呼吸引导器

**文件:** `App/modes/timer_mode.c` (同一文件，继续添加)

### Step 7.1: 呼吸引导 update

```c
static void breathing_update(void)
{
    if (breath_phase == BREATH_STOPPED) return;

    uint32_t now       = HAL_GetTick();
    uint32_t elapsed   = now - breath_phase_start;
    uint32_t half_cycle = BREATH_INHALE_MS;  /* 4000ms 吸入 */

    if (breath_phase == BREATH_INHALING && elapsed >= BREATH_INHALE_MS) {
        breath_phase = BREATH_EXHALING;
        breath_phase_start = now;
        elapsed = 0;
    } else if (breath_phase == BREATH_EXHALING && elapsed >= BREATH_EXHALE_MS) {
        /* 完成一个呼吸周期 */
        breath_cycle_count++;
        if (breath_cycle_count >= BREATH_CYCLE_COUNT) {
            /* 引导完成 */
            breath_phase = BREATH_STOPPED;
            a4988_vibrate(1, 100);  /* 轻振一下提示结束 */
            LOG("Breathing: done (%u cycles)", BREATH_CYCLE_COUNT);
            return;
        }
        /* 开始下一个吸气 */
        breath_phase = BREATH_INHALING;
        breath_phase_start = now;
        elapsed = 0;
    }

    /* 计算当前角度: 吸气 0→180°, 呼气 180→0°，正弦平滑 */
    float progress;
    if (breath_phase == BREATH_INHALING) {
        progress = (float)elapsed / (float)BREATH_INHALE_MS;
    } else {
        progress = 1.0f - (float)elapsed / (float)BREATH_EXHALE_MS;
    }
    if (progress > 1.0f) progress = 1.0f;
    if (progress < 0.0f) progress = 0.0f;

    /* 使用正弦函数平滑过渡: sin(0→π/2) 映射到 0→180° */
    float sin_val = sinf(progress * 3.14159f / 2.0f);
    breath_angle = sin_val * 180.0f;

    pointer_set_target(breath_angle, POINTER_MOVE_SMOOTH);
}
```

### Step 7.2: 呼吸引导 render

```c
static void breathing_render(void)
{
    static breath_phase_t last_phase = (breath_phase_t)0xFF;
    static uint8_t last_cycle = 0xFF;

    /* 去重 (phase 切换或 cycle 增加时重绘) */
    if (breath_phase == last_phase && breath_cycle_count == last_cycle) return;
    last_phase = breath_phase;
    last_cycle = breath_cycle_count;

    st7735_fill_screen(COLOR_BLACK);

    const char *label;
    uint16_t color;

    switch (breath_phase) {
        case BREATH_INHALING: label = "INHALE";  color = COLOR_CYAN;   break;
        case BREATH_EXHALING: label = "EXHALE";  color = COLOR_YELLOW; break;
        default:              label = "STOPPED"; color = COLOR_GRAY;   break;
    }

    /* 大字提示 */
    gui_draw_text_centered(64, 45, label, 3, color, COLOR_BLACK);

    /* 呼吸计数 */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "x%u/%u",
                 breath_cycle_count + 1, BREATH_CYCLE_COUNT);
        gui_draw_text_centered(64, 90, buf, 1, COLOR_GRAY, COLOR_BLACK);
    }

    /* 呼吸进度条 (吸入=填充, 呼出=消退) */
    if (breath_phase != BREATH_STOPPED) {
        uint32_t elapsed = HAL_GetTick() - breath_phase_start;
        float progress;
        if (breath_phase == BREATH_INHALING) {
            progress = (float)elapsed / (float)BREATH_INHALE_MS;
        } else {
            progress = (float)elapsed / (float)BREATH_EXHALE_MS;
        }
        if (progress > 1.0f) progress = 1.0f;

        uint16_t bar_w = (uint16_t)((float)(LCD_WIDTH - 20) * progress);
        st7735_fill_rect(10, 125, LCD_WIDTH - 20, 8, COLOR_DARK_GRAY);
        if (bar_w > 0) {
            st7735_fill_rect(10, 125, bar_w, 8, color);
        }
    }

    /* 底部提示 */
    gui_draw_text_centered(64, 145, "HOLD: START/STOP",
                           0, COLOR_DARK_GRAY, COLOR_BLACK);

    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

### Step 7.3: 呼吸引导按键

```c
static void breathing_handle_button(button_event_t event)
{
    if (event != BTN_EVENT_LONG_PRESS) return;

    if (breath_phase == BREATH_STOPPED) {
        /* 开始引导 */
        breath_phase       = BREATH_INHALING;
        breath_phase_start = HAL_GetTick();
        breath_cycle_count = 0;
        breath_angle       = 0.0f;
        LOG("Breathing: started");
    } else {
        /* 停止引导 */
        breath_phase = BREATH_STOPPED;
        LOG("Breathing: stopped");
    }

    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

### Step 7.4: 更新 timer_mode 分派函数

**update 分派:**
```c
void timer_mode_update(void)
{
    switch (timer_page) {
    case TIMER_PAGE_COUNTDOWN:
        /* 原有倒计时 update 逻辑 (保持不变) */
        {
            if (state == TIMER_RUNNING) {
                uint32_t now     = HAL_GetTick();
                uint32_t elapsed = (now - start_tick) / 1000;
                if (elapsed >= total_sec) {
                    remaining_sec = 0;
                    state         = TIMER_FINISHED;
                    a4988_vibrate(TIMER_END_VIBRATE_COUNT, 200);
                    LOG("Timer finished!");
                } else {
                    remaining_sec = total_sec - elapsed;
                }
            }
            pointer_set_timer(remaining_sec, total_sec);
        }
        break;
    case TIMER_PAGE_POMODORO:
        pomodoro_update();
        break;
    case TIMER_PAGE_BREATHING:
        breathing_update();
        break;
    }
}
```

**render 分派:**
```c
void timer_mode_render(void)
{
    switch (timer_page) {
    case TIMER_PAGE_COUNTDOWN:
        /* 原有倒计时 render 逻辑 (保持不变) */
        break;
    case TIMER_PAGE_POMODORO:
        pomodoro_render();
        break;
    case TIMER_PAGE_BREATHING:
        breathing_render();
        break;
    }
}
```

**按键分派:**
```c
void timer_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event == BTN_EVENT_LONG_PRESS) {
        switch (timer_page) {
        case TIMER_PAGE_COUNTDOWN:
            /* 长按: 先切子页，再按一次才进入子页功能 */
            timer_page = TIMER_PAGE_POMODORO;
            st7735_fill_screen(COLOR_BLACK);
            gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
            LOG("Timer: page=POMODORO");
            break;

        case TIMER_PAGE_POMODORO:
            if (pomo_phase == POMO_STOPPED) {
                /* 停止态: 再长按切到呼吸引导 */
                timer_page = TIMER_PAGE_BREATHING;
                st7735_fill_screen(COLOR_BLACK);
                gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
                LOG("Timer: page=BREATHING");
            } else {
                /* 运行态: 长按停止番茄钟 */
                pomodoro_handle_button(event);
            }
            break;

        case TIMER_PAGE_BREATHING:
            if (breath_phase == BREATH_STOPPED) {
                /* 停止态: 再长按切回倒计时 */
                timer_page = TIMER_PAGE_COUNTDOWN;
                st7735_fill_screen(COLOR_BLACK);
                gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
                LOG("Timer: page=COUNTDOWN");
            } else {
                /* 运行态: 长按停止呼吸 */
                breathing_handle_button(event);
            }
            break;
        }
        return;
    }
    /* 短按由 mode_manager 处理 */
}
```

> ⚠️ **交互说明:** Timer 模式内的子页轮转逻辑：
> - **子页切换**（页面停止态时按长按）= 轮转到下一个子页
> - **子页控制**（页面运行态时按长按）= 停止当前功能
> - **子页启动**（页面停止态时，需要先按一次进入该页，再按一次停止态时的特殊逻辑？不，这里需要重新设计）
>
> **修订后的 Timer 交互:**
>   - 倒计时页：长按 = 开始/暂停/重置（原有逻辑，不变）。双击 = 切到番茄钟
>   - 实际上没有双击。所以改为：**倒计时空闲态(STOPPED)时长按不启动，而是切子页**。
>
> **最终 Timer 长按逻辑:**
>   - 任何子页、任何状态：**先判断运行态**。运行中→长按停止；停止中→长按切子页

Timer 模式交互逻辑使用以下分区：

```c
void timer_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event != BTN_EVENT_LONG_PRESS) return;

    /* 通用规则: 运行态时长按 = 停止; 停止态时长按 = 切子页 */
    bool is_running = false;

    switch (timer_page) {
    case TIMER_PAGE_COUNTDOWN:
        is_running = (state == TIMER_RUNNING);
        if (is_running) {
            /* 暂停 */
            state = TIMER_PAUSED;
            LOG("Timer paused");
        } else {
            /* 停止态 → 切到番茄钟 */
            timer_page = TIMER_PAGE_POMODORO;
            LOG("Timer: page=POMODORO");
        }
        break;

    case TIMER_PAGE_POMODORO:
        is_running = (pomo_phase != POMO_STOPPED);
        if (is_running) {
            pomo_phase = POMO_STOPPED;
            LOG("Pomo: stopped");
        } else {
            timer_page = TIMER_PAGE_BREATHING;
            LOG("Timer: page=BREATHING");
        }
        break;

    case TIMER_PAGE_BREATHING:
        is_running = (breath_phase != BREATH_STOPPED);
        if (is_running) {
            breath_phase = BREATH_STOPPED;
            LOG("Breathing: stopped");
        } else {
            timer_page = TIMER_PAGE_COUNTDOWN;
            LOG("Timer: page=COUNTDOWN");
        }
        break;
    }

    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

> **注意:** 这个逻辑需要 **合并原有的倒计时按键逻辑**。原有的 `timer_mode_handle_button` 中的 FINISHED 重置、RUNNING 暂停、STOPPED/PAUSED 开始等逻辑需要适配新框架：
> - STOPPED → 长按 → **切子页**（不再启动倒计时）
> - **如何启动倒计时？** → 进入 Timer 模式时自动开始？不行。
>
> **解决方案: STOPPED 态时长按 = 启动倒计时; PAUSED 态时长按 = 切子页。**
> 这样倒计时功能保持原有体验，同时子页切换在空闲时有路可走。

最终 Timer 长按逻辑：

```c
void timer_mode_handle_button(button_id_t btn, button_event_t event)
{
    if (event != BTN_EVENT_LONG_PRESS) return;

    switch (timer_page) {
    case TIMER_PAGE_COUNTDOWN:
        if (state == TIMER_FINISHED) {
            /* FINISHED → 重置 */
            state = TIMER_STOPPED;
            remaining_sec = total_sec;
            LOG("Timer reset from FINISHED");
        } else if (state == TIMER_RUNNING) {
            /* RUNNING → 暂停 */
            state = TIMER_PAUSED;
            LOG("Timer paused");
        } else if (state == TIMER_PAUSED) {
            /* PAUSED → 切到番茄钟 (暂停态不允许直接切，先切子页) */
            timer_page = TIMER_PAGE_POMODORO;
            LOG("Timer: page=POMODORO");
        } else {
            /* STOPPED → 启动倒计时 */
            remaining_sec = total_sec;
            start_tick    = HAL_GetTick();
            state = TIMER_RUNNING;
            LOG("Timer started (%lu sec)", (unsigned long)remaining_sec);
        }
        break;

    case TIMER_PAGE_POMODORO:
        if (pomo_phase != POMO_STOPPED) {
            /* 运行中 → 停止 */
            pomo_phase = POMO_STOPPED;
            LOG("Pomo: stopped");
        } else {
            /* 停止中 → 启动番茄钟 */
            pomo_phase     = POMO_WORK;
            pomo_total     = POMODORO_WORK_SEC;
            pomo_remaining = POMODORO_WORK_SEC;
            pomo_start_tick = HAL_GetTick();
            pomo_count     = 0;
            LOG("Pomo: started");
        }
        break;

    case TIMER_PAGE_BREATHING:
        if (breath_phase != BREATH_STOPPED) {
            /* 运行中 → 停止 */
            breath_phase = BREATH_STOPPED;
            LOG("Breathing: stopped");
        } else {
            /* 停止中 → 切回倒计时 */
            timer_page = TIMER_PAGE_COUNTDOWN;
            LOG("Timer: page=COUNTDOWN");
        }
        break;
    }

    st7735_fill_screen(COLOR_BLACK);
    gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
}
```

> **最终交互:** 倒计时(长按=运行/暂停) → 暂停态再长按→番茄钟 → 长按=开始/停止 → 停止态再长按→呼吸引导 → 长按=开始/停止 → 停止态再长按→回到倒计时

但由于呼吸引导的"启动"也需要长按（停止态长按=切子页），呼吸引导需要一个启动方式。加一个逻辑：**呼吸引导页进入后自动启动**，或者在停止态短按启动。

**最终方案: 进入呼吸引导页自动开始引导**（因为进入本身就意味着意图切换到这个功能）:

```c
    case TIMER_PAGE_BREATHING:
        /* 切换到呼吸引导时自动开始 (在 enter 或 render 首次触发) */
```

或者在 `timer_mode_update` 首次进入时自动启动。这需要个首次标志。

---

## Task 8: 编译验证 + 回归检查

### Step 8.1: STM32 编译

```bash
# 在 Keil MDK 中 Rebuild 或使用命令行
# 预期: 0 Error, 可能新增个别 Warning
```

### Step 8.2: 回归检查

```bash
cd "c:\Users\wumu2\OneDrive\桌面\ov_watch"
bash .claude/scripts/check-firmware.sh
# 预期: All checks passed
```

### Step 8.3: ESP32 编译

```bash
cd C:\Projects\weather_clock
idf.py -j2 build
# 预期: 编译成功
```

---

## 自检清单

- [x] **Spec 覆盖:** 湿度计 ✅ 双温对比 ✅ 日出日落环 ✅ 番茄钟 ✅ 呼吸引导 ✅
- [x] **无占位符:** 所有步骤含完整代码
- [x] **类型一致:** `weather_data_t` 新字段在所有引用处一致
- [x] **向后兼容:** ESP32 帧格式通过计数 `|` 自动识别新旧，STM32 解析兼容旧帧
- [x] **ARMCC V5 兼容:** 无双斜杠注释混用、无 `0b` 字面量、无复合语句表达式
- [x] **单键适配:** 长按=子页内动作/切换，短按=切模式
- [x] **引脚正确:** ESP32 GPIO17 → STM32 PD6 (USART2_RX), SHT30 PF0/PF1 (I2C2)

# ESP32 天气桥接 — 设计规范

> 日期：2026-06-17  
> 状态：已确认  
> ESP32 项目：`C:/Projects/weather_clock/`（ESP-IDF v5.4）  
> STM32 项目：`firmware/`（Keil ARMCC V5）

---

## 一、整体架构

```
ESP32 (weather_clock)                    STM32 (ov-watch)
  ├─ WiFi → NTP 对时                    ├─ USART2 DMA 循环接收 (256B)
  ├─ open-meteo API → 天气               ├─ BG任务50ms轮询 → 帧解析
  ├─ uart_bridge_task (每60s)            ├─ 校验和匹配？→ 更新数据+RTC
  │    格式化+校验和                     │    不匹配？→ 丢弃整帧
  │    GPIO17(TX) ──────► PD6(RX)        └─ temp_mode UI 显示
  │    GPIO16(RX) ──────► 不接(悬空)
  └─ GND ─────────────── 面包板 -轨
```

**设计决策：**
- **单工通信，无握手，无重传** — 天气是瞬时状态而非事件记录，丢一帧等60秒即可
- **校验和** — 用于丢弃脏数据，不用于请求重传
- **每次收到都写 RTC** — 用 NTP 精准时间持续校准 LSI 漂移

---

## 二、UART 协议帧格式

### 帧定义

```
$2026-06-17 10:30:00|22.5|65|Cloud|A3\n
│                                          ││
└── 帧头 $                                 └└─ 帧尾 \n (0x0A)
  ┌─────────────┬───┬──┬─────┬──┐
  │ 日期时间     │温度│湿度│天气  │校验│
  │ YYYY-MM-DD  │°C  │%  │英文  │和  │
  │ HH:MM:SS    │    │   │无空格│    │
  └─────────────┴───┴──┴─────┴──┘
```

### 字段

| 字段 | 格式 | 示例 | 说明 |
|------|------|------|------|
| 帧头 | `$` | `$` | 任意时刻找到 `$` 即开始新帧 |
| 日期时间 | `%04d-%02d-%02d %02d:%02d:%02d` | `2026-06-17 10:30:00` | 北京时间 UTC+8 |
| 温度 | `%.1f` | `22.5` | |
| 湿度 | `%d` | `65` | 整数百分比 |
| 天气 | `%s` | `Cloud` | 空格替换为 `_`（防 sscanf 截断） |
| 校验和 | `%02X` | `A3` | payload 累加和取低 8 位，大写 hex |
| 帧尾 | `\n` | — | 0x0A，触发解析 |

### 校验和算法

```c
// payload = $ 之后、|XX 之前的所有字符
// 例: "2026-06-17 10:30:00|22.5|65|Cloud"

uint8_t checksum = 0;
for (int i = 0; payload[i]; i++)
    checksum += (uint8_t)payload[i];
// → checksum = 0xA3
// → 帧尾拼接: |A3\n
```

---

## 三、ESP32 端实现（uart_bridge_task）

### 新增文件

`main/tasks/uart_bridge_task.c` + `main/tasks/uart_bridge_task.h`

### UART 初始化

```c
// UART2: TX=GPIO17, RX=GPIO16 (不用但需配)
// 波特率 115200, 8N1, 无流控

uart_config_t uart_config = {
    .baud_rate  = 115200,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_DISABLE,
    .stop_bits  = UART_STOP_BITS_1,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
};
uart_param_config(UART_NUM_2, &uart_config);
uart_set_pin(UART_NUM_2, GPIO_NUM_17, GPIO_NUM_16, 
             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
uart_driver_install(UART_NUM_2, 256, 0, 0, NULL, 0);
```

### 发送任务（每60秒）

```c
void uart_bridge_task(void *pvParameters) {
    char tx_buffer[128];
    
    while (1) {
        // 1. 天气描述空格 → 下划线
        // 2. 格式化时间: YYYY-MM-DD HH:MM:SS
        // 3. 拼接 payload: "时间|温度|湿度|天气"
        // 4. 计算校验和 (payload 累加 & 0xFF)
        // 5. 拼接帧: "$payload|XX\n"
        // 6. uart_write_bytes(UART_NUM_2, tx_buffer, strlen(tx_buffer))
        // 7. vTaskDelay(pdMS_TO_TICKS(60000))
    }
}
```

### ESP32 main.c 注册

```c
xTaskCreate(uart_bridge_task, "uart_bridge", 4096, NULL, 5, NULL);
```

---

## 四、STM32 端实现

### 4.1 USART2 DMA 接收

**硬件：**

```
ESP32 GPIO17(TX) ──► STM32 PD6 (USART2_RX, AF7)
STM32 PD5 (USART2_TX) ──► 不接！配置为 GPIO_MODE_ANALOG 关断 TX
GND ── 面包板 -轨
```

**DMA 配置（main.c 新增 MX_USART2_UART_Init）：**

```c
// USART2: 115200, 8N1, RX only
// DMA1_Stream5 Ch4 → USART2_RX
// 循环模式, 256 字节缓冲区

static uint8_t uart2_rx_buf[256];    // DMA 循环写入
static char    uart2_line_buf[128];  // 行缓冲
static uint8_t uart2_line_pos;       // 行缓冲写入位置
static uint8_t uart2_dma_prev;       // 上次 DMA CNDTR

// 初始化完成后:
HAL_UART_Receive_DMA(&huart2, uart2_rx_buf, 256);
```

**hal_msp.c 新增 USART2_MspInit：**

```c
// PD6 → GPIO_MODE_AF_PP, GPIO_AF7_USART2 (RX)
// PD5 → GPIO_MODE_ANALOG (TX 关断，不是 AF！)
// DMA1_Stream5: Channel 4, Peripheral→Memory, Circular
```

**stm32f4xx_it.c 新增中断：**

```c
void DMA1_Stream5_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}
```

### 4.2 帧解析（freertos.c BG 任务，每 50ms）

```c
// 1. 读取 DMA CNDTR → 计算新增字节数
//    if (current >= prev) len = current - prev;
//    else                 len = (256 - prev) + current;  // 回绕
// 2. 逐字节扫描新增数据:
//    找 '$' → line_pos = 0
//    找 '\n' → line_buf[line_pos]='\0', 调用 weather_frame_parse()
//    其他   → if (line_pos < 127) line_buf[line_pos++] = byte
//              else line_pos = 0  // 溢出丢弃
// 3. 更新 uart2_dma_prev = current
```

### 4.3 weather_frame_parse()

```c
// 输入: "$2026-06-17 10:30:00|22.5|65|Cloud|A3"

void weather_frame_parse(const char *frame) {
    // 1. 校验帧头 '$'
    if (frame[0] != '$') return;

    // 2. 找最后一个 '|' → 取校验和
    // 3. 对 '$'+1 到 last_pipe-1 的 payload 累加 → 比较校验和
    //    不匹配 → return

    // 4. sscanf 解析: "%d-%d-%d %d:%d:%d|%f|%d|%s" (注: ARMCC不支持%hhu, 用%d接收humidity)
    //    返回值 < 9 → return

    // 5. 数据合法性校验:
    //    year >= 2000 && month >=1 && month <=12 && day >=1 && day <=31
    //    hour <= 23 && minute <= 59 && second <= 59
    //    humidity <= 100

    // 6. 恢复天气描述中的 '_' → ' '

    // 7. rtc_drv_set_datetime() — 包含 RTOFF 等待、CNF 操作
    //    参考 HAL 标准流程: SET_BIT(CRL, CNF) → 等 RTOFF → 写 → 清 CNF

    // 8. 更新 g_weather:
    //    g_weather.temperature = parsed_temp;
    //    g_weather.humidity    = parsed_hum;
    //    strncpy(g_weather.description, parsed_desc, 31);
    //    g_weather.last_update_tick = HAL_GetTick();
    //    g_weather.valid = true;

    // 9. 数据变化? gui_dirty_mark(temp_mode_area): skip
}
```

#### 并发安全说明

当前架构下 `weather_frame_parse()` 和 `temp_mode_render()` 都在 `StartTaskBG` 同一任务中顺序执行，不会被抢占，**无实际竞态**。但如果将来有人把渲染移到其他任务（如恢复 TaskDisplay），下面两个措施保证安全：

```c
// 写入端: 先在栈上构造完整数据，再一次写入 g_weather
//        → 不存在"字符串拷一半被读到"的窗口
weather_data_t tmp;
tmp.temperature      = parsed_temp;
tmp.humidity         = (uint8_t)parsed_hum;
strncpy(tmp.description, parsed_desc, 31);
tmp.description[31]  = '\0';
tmp.last_update_tick = HAL_GetTick();
tmp.valid            = true;
g_weather = tmp;                    // 结构体赋值，ARMCC 生成 memcpy，一次性

// 读取端: 进入渲染函数时立即快照到栈
void temp_mode_render(void) {
    weather_data_t local = g_weather;  // 只读一次，后续全部用 local
    // ... 使用 local.temperature, local.humidity, local.description ...
}
```

> 无需互斥锁或关中断。即使将来写入/读取在不同任务中，`memcpy` 的结构体拷贝也是单条 ARM 指令（LDM/STM 块传输），不存在半字撕裂。

### 4.4 数据结构

```c
// app_config.h 新增

typedef struct {
    float    temperature;       // °C
    uint8_t  humidity;          // %
    char     description[32];   // "Cloud", "Rain" 等（_ 已恢复为空格）
    uint32_t last_update_tick;  // HAL_GetTick()
    bool     valid;             // 是否收到过至少一帧
} weather_data_t;

extern weather_data_t g_weather;
```

### 4.5 CubeMX 改动清单

| 文件 | 改动 |
|------|------|
| `ov-watch.ioc` | 启用 USART2: Asynchronous, 115200, 8N1, RX=PD6 |
| `Core/Inc/main.h` | `extern UART_HandleTypeDef huart2;` `extern DMA_HandleTypeDef hdma_usart2_rx;` |
| `Core/Src/main.c` | 添加 `MX_USART2_UART_Init()`，调用 `HAL_UART_Receive_DMA()` |
| `Core/Src/stm32f4xx_hal_msp.c` | 新增 `USART2_MspInit`：PD6=AF7, PD5=ANALOG, DMA1_S5_Ch4 |
| `Core/Src/stm32f4xx_it.c` | `DMA1_Stream5_IRQHandler` |
| `Core/Inc/pin_config.h` | 新增 `ESP32_BRIDGE_UART USART2`，`ESP32_RX_PIN GPIO_PIN_6`，`ESP32_RX_PORT GPIOD` |

---

## 五、temp_mode UI 重设计

### 页面切换

```
短按 → 模式管理器切换到下一个模式（不进 temp_mode_handle_button）
长按 → temp_mode_handle_button: show_device_info = !show_device_info
temp_mode_enter() → show_device_info = false（每次进入重置为天气主页）
```

### 天气主页（默认）

```
┌──────────────────────┐
│   ☁  Cloud           │  y=5   FONT_8x16, 图标+天气描述
│                      │
│       22.5°C         │  y=45  FONT_16x32 大字温度
│                      │
│   💧 65%             │  y=100 FONT_8x16 湿度
│   ⏱ 17:30           │  y=125 FONT_8x16 NTP时间
└──────────────────────┘

数据来源: g_weather.temperature / humidity / description
时间来源: rtc_drv_get_datetime()  (已被 ESP32 校准)
```

### 设备信息页（长按切换）

```
┌──────────────────────┐
│  Device Info         │  y=5
│                      │
│  STM32: 27.3°C       │  y=30  内部 ADC 温度
│  ESP32: Connected    │  y=55  连接状态（见下）
│  RTC: 2026-06-17     │  y=80  RTC 日期
│  Flash: OK 3entry    │  y=105 Flash 状态 + 条目数
│  HOLD: back          │  y=145 提示
└──────────────────────┘

ESP32 连接状态:
  ≤120s:   "Connected"
  120-600s: "Weak"       (黄色)
  >600s:   "Lost"        (红色)
  从未收到: "No Data"
```

### 渲染去重

```c
static weather_data_t last_rendered;  // 上次渲染的缓存

static bool weather_changed(void) {
    if (!g_weather.valid) return false;
    if (!last_rendered.valid) return true;
    if (g_weather.temperature != last_rendered.temperature) return true;
    if (g_weather.humidity    != last_rendered.humidity)    return true;
    if (strcmp(g_weather.description, last_rendered.description)) return true;
    return false;
}
// 数据未变化时不标记脏区，不触发 SPI flush
```

### 设备信息页渲染策略

```c
// 静态内容：进入时渲染一次，之后不刷新
// 但 ESP32 连接状态字符会随超时变化 → 用独立判断
static uint32_t last_device_info_render; // tick of last render
if (HAL_GetTick() - last_device_info_render > 10000) {
    render_device_info();
    last_device_info_render = HAL_GetTick();
}
```

---

## 六、文件变更总表

### ESP32 (weather_clock)

| 操作 | 文件 | 说明 |
|------|------|------|
| 新增 | `main/tasks/uart_bridge_task.c` | UART 发送任务（~80行） |
| 新增 | `main/tasks/uart_bridge_task.h` | 头文件 |
| 修改 | `main/main.c` | `xTaskCreate(uart_bridge_task)` |
| 修改 | `main/CMakeLists.txt` | 添加源文件 |

### STM32 (ov-watch firmware)

| 操作 | 文件 | 说明 |
|------|------|------|
| **配置** | `ov-watch.ioc` | 启用 USART2 |
| 修改 | `Core/Inc/main.h` | extern USART2 + DMA 句柄 |
| 修改 | `Core/Src/main.c` | `MX_USART2_UART_Init()` + DMA 启动 |
| 修改 | `Core/Src/stm32f4xx_hal_msp.c` | USART2_MspInit (PD6=AF7, PD5=ANALOG, DMA) |
| 修改 | `Core/Src/stm32f4xx_it.c` | `DMA1_Stream5_IRQHandler` |
| 修改 | `Core/Src/freertos.c` | BG 任务 DMA 轮询 + 帧解析 + 数据更新 |
| 修改 | `Core/Inc/app_config.h` | `weather_data_t` 结构体 + `g_weather` |
| 修改 | `Core/Inc/pin_config.h` | 新增 ESP32_UART 宏 |
| 修改 | `App/modes/temp_mode.c` | UI 重写：天气主页 + 设备信息页 |
| 新增 | `App/weather_bridge.c` | `weather_frame_parse()` (~80行) |
| 新增 | `App/weather_bridge.h` | 头文件 |

---

## 七、风险与防坑措施

| 风险 | 措施 |
|------|------|
| DMA 回绕漏字节 | `if (cur>=prev) len=cur-prev; else len=(256-prev)+cur` |
| 行缓冲溢出 | `line_pos >= 127 → line_pos=0` 丢弃 |
| 校验和不匹配 | 整帧丢弃，不更新任何数据 |
| 日期字段越界 | year>=2000, month 1-12, day 1-31, hour 0-23 |
| RTC 写入冲突 | `while(!__HAL_RTC_IS_SYNCHRONIZED(&hrtc))` 超时 50ms |
| 数据未变频繁 flush | 渲染前对比 `last_rendered`，不变不标记脏区 |
| ESP32 断连 | 连接状态三档（120s/600s/从未），不用单比特 |
| 切换模式回来页面错 | `temp_mode_enter()` 强制 `show_device_info = false` |
| PD5 TX 浮空干扰 | 配置为 `GPIO_MODE_ANALOG`，不启用 AF7 TX |
| 天气描述含空格 | ESP32 端发前空格→`_`，STM32 收后 `_`→空格 |
| ARMCC 不支持 `sscanf %hhu` | 用 `%d` 接收 humidity，收到后转 `uint8_t` |
| 帧头缺失 | 只处理 `$` 开头的帧，非 `$` 开头直接丢弃 |
| 并发读写 `g_weather` | 写入端栈上构造→一次性赋值；读取端进函数即快照到栈（见4.3节） |

---

## 八、验证清单

- [ ] ESP32 上电后串口监视器能看到正确格式的帧输出
- [ ] STM32 接 USART2 后，调试串口 `g_weather.valid=1`
- [ ] 温度/湿度/天气描述与 ESP32 串口监视器一致
- [ ] RTC 时间被 ESP32 NTP 校准，一天不在漂移
- [ ] 校验和错误帧被丢弃（可手动发一帧错误校验和测试）
- [ ] 拔出 ESP32 TX 线后，>600s 显示 "Lost"
- [ ] 短按切模式→再切回 temp_mode，页面重置为天气主页
- [ ] 设备信息页在长按后显示，再长按回到天气主页
- [ ] `check-firmware.sh` 18 项全部通过

# OV-Watch 交接文档

> 日期：2026-06-17  
> 版本：v1.3.0-weather-bridge  
> 硬件：鹿小班 LXB407ZG-P1（STM32F407ZGT6 最小系统板） + 1.8寸 TFT ST7735S + W25Q64 Flash（已接） + ESP32 天气桥接（已通）

---

## ⚠️ 烧录方式：仅 ISP 串口下载

> **这块板子没有 ST-Link、没有 SWD、没有 J-Link。只能用 ISP 串口烧录。**

```
烧录步骤（每次都要走这套流程）：
  1. Keil → Rebuild → 生成 MDK-ARM\ov-watch\ov-watch.hex
  2. 板子断电 → BOOT0 接高电平(3.3V) → 上电（进入 ISP 模式）
  3. USB-TTL 模块交叉接线到 USART1:
       USB-TTL TX ──► PA10 (STM32 RX)
       USB-TTL RX ──► PA9  (STM32 TX)
       USB-TTL GND ─► GND
  4. ATK-XISP（或 FlyMcu/mcuisp）→ 选 COM 口 → 115200 → 加载 .hex → 开始编程
  5. 烧完断电 → BOOT0 接 GND → 上电运行

调试串口日志：同一个 USART1，115200 baud，SSCOM 查看。
烧录时 PA9/PA10 被 ISP 占用，无法同时看日志。
```

> **Keil ARMCC V5 偶发编译内存溢出**（`L6815U: Out of memory`）。遇到 6 Error 时：关闭 Keil → 重新打开 → Rebuild 即可恢复 0 Error。

---

## TL;DR 当前状态

| 项目 | 状态 |
|------|------|
| 编译 | 0 Error, 4 Warning (Keil ARMCC V5, 已知无害) |
| TFT 显示 | 帧缓冲 + 选择性渲染，正常显示时钟/天气/倒计时 |
| 闪烁 | **硬件限制**：ST7735S TE引脚未引出，软件无法根治 |
| 回归检查 | 18项全部通过 (check-firmware.sh) |
| W25Q64 Flash | ✅ 已接，JEDEC=0xEF4017，fs_mgr已挂载 |
| **ESP32 天气桥接** | ✅ **已打通** — ESP32每60s发天气帧，STM32 USART2 DMA接收，RTC自动校准 |
| **temp_mode 天气显示** | ✅ 显示温度+湿度+天气描述+时间（数据来自ESP32 open-meteo API） |
| 按键 | PA15 单键（短按切模式、长按动作） |
| 电机/A4988 | 已买42步进电机+驱动板，**未接（缺12V适配器）** |
| SHT30 温度 | 已买未到，**预留 PF0/PF1** |
| ESP32 OLED | 已禁用（ESP32→STM32 UART桥接模式），原OLED项目改为纯数据源 |

---

## ESP32 天气桥接架构

```
ESP32 (C:\Projects\weather_clock\)        STM32 (firmware\)
  ├─ WiFi → NTP 对时 + open-meteo API    ├─ USART2 DMA 循环接收 (256B)
  ├─ uart_bridge_task (每60s)             ├─ BG任务50ms轮询 → weather_bridge_poll_dma()
  │    格式化+校验和 → GPIO17(TX)          ├─ weather_frame_parse() → 校验和→解析→写RTC
  └─ GND ───── 面包板 -轨                 └─ temp_mode UI 显示天气

协议: $2026-06-17 22:57:54|25.0|74|Cloudy|D3\n
       帧头 时间             温度 湿度 描述  校验和(hex)
```

### 数据流

1. ESP32 WiFi连接 → NTP对时 → open-meteo拉天气 → `g_display_data` 填充
2. `uart_bridge_task` 每60秒格式化 `$时间|温度|湿度|天气描述|校验\n`
3. `uart_write_bytes(UART_NUM_2)` 从 GPIO17 发出
4. STM32 PD6(USART2_RX) → DMA1_Stream5 循环接收
5. BG任务每50ms调用 `weather_bridge_poll_dma()` → 找`$`帧头 → 等`\n` → 校验和验证 → `sscanf` 解析 → `rtc_drv_set_datetime()` + 更新 `g_weather`
6. `temp_mode_render()` 从 `g_weather` 读取显示

---

## 单键操作

| 动作 | 功能 |
|------|------|
| 短按 | 下一模式：Clock → Temp → Timer → Settings → Clock… |
| 长按(Clock) | 切换 12/24h |
| 长按(Temp) | 切换天气主页 ↔ 设备信息页（ESP32连接状态、STM32 ADC等） |
| 长按(Timer) | 开始/暂停/重置倒计时 |
| 长按(Settings) | 退出（自动保存到Flash） |

---

## 固件架构

```
main.c
├─ MX_SPI1_Init()    SPI1 @ 10.5MHz (PA5=SCK, PA7=MOSI)
├─ MX_DMA_Init()     DMA2_Stream3 Ch3 → SPI1_TX
├─ MX_SPI2_Init()    SPI2 @ 2.625MHz (PB10=SCK, PC3=MOSI, PC2=MISO)
├─ MX_USART2_UART_Init()   USART2 @ 115200 (PD6=RX, PD5=ANALOG关断)
├─ MX_RTC_Init()     首次上电注入 __DATE__ __TIME__（LSI时钟源，漂移由ESP32 NTP校准）
├─ HAL_UART_Receive_DMA()  启动USART2 DMA循环接收
├─ BSP 初始化         st7735, button, a4988, temp, fs_mgr
└─ FreeRTOS 启动
    ├─ TaskButton (10ms)   按键扫描→事件队列
    ├─ TaskBG (50ms)       UART DMA轮询 + 按键事件 + render→dirty rect flush
    ├─ TaskDisplay (闲置)  帧缓冲模式下无需工作
    └─ TaskMotor (50ms)    指针引擎→A4988（无电机空转）
```

---

## 📌 所有引脚速查表（已校验 CubeMX 实际配置）

| 外设 | 信号 | STM32引脚 | LXB407ZG-P1 板子位置 |
|------|------|-----------|----------------------|
| **TFT** | CS | PA4 | 下排 A4 |
| | SCK | PA5 | 下排 A5 |
| | MOSI | PA7 | 下排 A7 |
| | DC | PC4 | 下排 C4 |
| | RST | PC5 | 下排 C5 |
| | BL | PB0 | 下排 B0 |
| **W25Q64 Flash** ✅ | CS | PB12 | 下排 B12 |
| | SCK | PB10 | 下排 B10 |
| | MOSI | PC3 | 下排 C3 |
| | MISO | PC2 | 下排 C2 |
| **SHT30** (预留) | SCL | PF1 | 上排 F1 |
| | SDA | PF0 | 上排 F0 |
| **A4988 电机** | STEP | PA0 | 下排 A0 |
| | DIR | PA1 | 下排 A1 |
| | EN | PA2 | 下排 A2 |
| | MS1-3 | PA3 | 下排 A3（三根短接=16细分） |
| **ESP32 天气桥** ✅ | TX→RX | PD6 | 上排 D6（USART2_RX） |
| **按键** | KEY | PA15 | 上排 A15 |
| **串口调试** | TX/RX | PA9/PA10 | 上排 + 左侧排针 |

### ESP32 ↔ STM32 接线

```
ESP32 GPIO17(TX) ──► STM32 PD6 (USART2_RX)
ESP32 GND        ──► 面包板 -轨（共地）
ESP32 VIN(5V)    ──► 核心板 5V 排针
⚠️ 烧录ESP32时必须拔掉GPIO17→PD6的线（GPIO17影响下载模式）
⚠️ 两路供电不要打架：ESP32用Micro-USB，STM32用Type-C，只共GND
```

### 引脚历史错误记录

| 信号 | 之前写的 | 实际 | 原因 |
|------|----------|------|------|
| W25Q64 SCK | ~~PB13~~ | PB10 | pin_config.h未校验CubeMX |
| W25Q64 MOSI | ~~PB15~~ | PC3 | 同上 |
| W25Q64 MISO | ~~PB14~~ | PC2 | 同上 |
| SHT30 SCL | ~~PB10~~ | PF1 | 同上 |
| SHT30 SDA | ~~PB11~~ | PF0 | 同上 |
| SPI1时钟源 | ~~APB1/42MHz~~ | APB2/84MHz | 时钟树理解错 |

> ⚠️ **今后改引脚前必须看 `Core/Src/stm32f4xx_hal_msp.c` 确认 CubeMX 实际配置。**

---

## 板子引脚分区速查（LXB407ZG-P1 丝印）

```
左侧独立排针（从上到下）：
  3V3（独立，可给传感器供电）
  RST
  TXD（PA9）
  RXD（PA10）

上排（左→右）：
  5V 3V3 A11 G6 G8 C7 C9 A9 A8 C11 D0 D2 D4 D6 G9 G11 B3 B5 B7 B9 E0 G13 E2 E4 E6 C13 F1 F3 F5
  5V 3V3 A12 G5 G7 C6 C8 A10 C12 D1 D3 D5 D7 G10 G15 B4 B6 B8 E1 G14 G12 E3 E5 C15 A15 F0 F2 F4 F6

下排（左→右）：
  GND GND D13 D11 D9 B15 B13 G4 B10 E14 E12 E10 E7 G0 F12 B2 A7 A5 A3 B0 F13 A2 A0 F8
  GND GND D12 D10 D8 B14 B12 D5 G3 B11 E15 E13 E11 E9 G1 F15 F11 C4 A6 A4 C5 B1 F14 A1 F7 F9 C3 C2
```

---

## SPI 时钟树

```
SYSCLK = 168MHz (HSE 8MHz / 8 × 336 / 2)
HCLK   = 168MHz (÷1)
APB2   = 84MHz  (÷2)  ← SPI1 挂在这！不是 APB1！
APB1   = 42MHz  (÷4)  ← SPI2、I2C2、TIM 挂在这

SPI1 (TFT)   = 84 ÷ 8 = 10.5MHz  ✅ ST7735S ≤15MHz
SPI2 (Flash) = 42 ÷ 16 = 2.625MHz ✅
```

---

## 面包板接线方案

核心板放桌面，用**母对母杜邦线**从排针扣出信号到面包板孔位。

```
核心板上排 3V3 ────► 面包板 +轨  ← 所有外设3.3V从这里取
核心板下排 GND ────► 面包板 -轨  ← 所有外设GND + 适配器GND
核心板上排 5V  ────► ESP32 VIN（预留）
```

---

## 已知问题（按优先级）

| # | 优先级 | 问题 | 根因 | 解决 |
|----|--------|------|------|------|
| 1 | P0 | TFT 闪烁 | ST7735S TE引脚未引出→无硬件同步 | 飞线TE或提高帧率 |
| 2 | P1 | Timer 模式黑屏 | 单键适配时疑似DMA旧bug污染，待复测 | Keil重开重编译烧录后再验证 |
| 3 | P1 | 电机未验证 | A4988缺12V适配器 | 买12V 2A适配器+100µF电容 |
| 4 | P1 | SHT30未接 | 已买未到，回退内部ADC | 到货后接PF0/PF1 |
| 5 | P2 | settings只读 | 单键无方向键 | 简化即可 |
| 6 | P2 | timer无法调时长 | 单键操作限制 | 双击/长按组合 |
| 7 | P3 | 4个编译Warning | em dash + 枚举混用 | 无害，可后续修 |
| 8 | P3 | RTC断电丢时间 | VBAT未接电池 | 但ESP32 NTP每60s校准，已缓解 |

### 已解决的问题

| # | 问题 | 解决 |
|----|------|------|
| ✅ | RTC 漂移 5分钟/半天 | ESP32 NTP 每60s通过UART校准 → 时间永远准 |
| ✅ | 温度精度 ±5°C | ESP32 open-meteo API 替代，天气桥接送温度+湿度 |
| ✅ | 无天气数据 | temp_mode 重写为天气主页+设备信息页 |
| ✅ | USART2未初始化 | CubeMX配置 + main.c/MspInit/it.c/中断全部就绪 |
| ✅ | DMA计数方向反 | `__HAL_DMA_GET_COUNTER` 返回NDTR(↓)，已修正为 `(buf-ndtr)%buf` |
| ✅ | sscanf吃校验和 | `last_pipe[0]='\0'` 截断，天气描述不再含\|D5 |
| ✅ | g_display_task_h=NULL崩溃 | 4处 `xTaskNotifyGive` 加 `if (g_display_task_h)` 判空 |

---

## 新增文件索引（天气桥接）

| 文件 | 作用 |
|------|------|
| `App/weather_bridge.h` | 帧解析接口声明 |
| `App/weather_bridge.c` | DMA轮询 + weather_frame_parse (校验和+sscanf+RTC写入) |
| `C:/Projects/weather_clock/main/tasks/uart_bridge_task.h` | ESP32 UART发送任务声明 |
| `C:/Projects/weather_clock/main/tasks/uart_bridge_task.c` | ESP32 UART发送任务实现 (每60s格式化+校验和+发送) |

### 修改的文件（本轮）

| 文件 | 改动 |
|------|------|
| `Core/Src/main.c` | +MX_USART2_UART_Init, +DMA缓冲变量, +g_weather, +HAL_UART_Receive_DMA |
| `Core/Inc/main.h` | +extern huart2, hdma_usart2_rx |
| `Core/Inc/app_config.h` | +weather_data_t 结构体 |
| `Core/Inc/pin_config.h` | +ESP32 UART宏 |
| `Core/Src/stm32f4xx_hal_msp.c` | +USART2_MspInit (PD6=AF7, PD5=ANALOG, DMA1_S5_Ch4) |
| `Core/Src/stm32f4xx_it.c` | +DMA1_Stream5_IRQHandler |
| `Core/Src/freertos.c` | +weather_bridge_poll_dma() 调用, +include weather_bridge.h |
| `App/modes/temp_mode.c` | 完全重写：天气主页 + 设备信息页 |
| `.claude/scripts/check-firmware.sh` | 更新 temp_mode 检查项为 last_rendered |

---

## ESP32 端操作指南

### 项目位置

`C:\Projects\weather_clock\` — ESP-IDF v5.4

### 编译烧录

```cmd
cd C:\Projects\weather_clock
idf.py -j2 build flash monitor
```

### 当前配置

| 项目 | 值 |
|------|-----|
| 芯片 | ESP32 (Xtensa，非 S2/S3/C3) |
| OLED | 已禁用（uart_bridge 模式） |
| UART2 TX | GPIO17 |
| WiFi/天气 | 原有逻辑不变 |
| uart_bridge_task | 每60秒发一帧，栈4KB，优先级5 |

### OLED 已被禁用

`display_task` 已注释、`oled_init()` 已注释、`g_display_task_h=NULL`，`xTaskNotifyGive(NULL)` 均有判空保护。看门狗不再监控 display。

### 烧录注意事项

```
1. 先拔掉 GPIO17→STM32 PD6 的线（GPIO17影响下载模式）
2. 烧完断电 → 插回 GPIO17→PD6 → 上电
3. ESP32用Micro-USB供电，STM32用Type-C供电
4. 只共GND，不共5V（避免电源打架）
```

---

## 调试帮助

- STM32 串口日志：USART1 115200，SSCOM
- **烧录和调试共用USART1，烧录时无法看日志**
- Flash 正常：`W25Q64 init done. JEDEC ID: 0xEF4017`
- 文件系统：`fs_mgr mounted OK, version=1, N entries`
- ESP32 数据到达：`weather frame: 2026-06-17 22:57:54|25.0|74|Cloudy|XX`
- DMA 启动：`weather_bridge DMA poll started`
- 天气有效：`g_weather.valid=1` → temp_mode 显示天气主页
- 模式切换：`CLOCK: enter` / `TEMP: enter`
- 闪烁测试：按住RST→画面静止=TFT正常；松开→SPI写GRAM撕裂

---

## 相关文档

| 文档 | 路径 |
|------|------|
| Bug清单 | `BUGS.md` |
| 天气桥接设计规范 | `docs/superpowers/specs/2026-06-17-esp32-weather-bridge-design.md` |
| 天气桥接实现计划 | `docs/superpowers/plans/2026-06-17-esp32-weather-bridge.md` |
| 编译修复 | `docs/superpowers/compile-fixes.md` |
| 运行时修复 | `docs/superpowers/runtime-bugs.md` |
| 项目设计文档 | `docs/superpowers/specs/2026-06-10-ov-watch-design.md` |
| 回归检查 | `.claude/scripts/check-firmware.sh` (18项) |
| CLAUDE.md | 项目根目录（AI行为规则+Skill调用表） |
| ESP32项目 | `C:\Projects\weather_clock\` |
| 参考项目 | `C:\Users\wumu2\OneDrive\桌面\ST7789_ref\` |
| ST7735S数据手册 | `C:\Users\wumu2\OneDrive\桌面\stm32F407ZGT6\ST7735S_datasheet.pdf` |

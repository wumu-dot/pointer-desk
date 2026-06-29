# pointer-desk 交接文档

> 日期：2026-06-29
> 版本：v2.0 — 6 模式完整 + 电机验证通过 + GDB 调试体系
> 硬件：鹿小班 LXB407ZG-P1（STM32F407ZGT6） + 1.8" TFT ST7735S + W25Q64 Flash + ESP32 天气桥接 + 42步进电机 (A4988)

---

## TL;DR 当前状态

| 项目 | 状态 |
|------|:--:|
| 编译 | 0 Error (GCC arm-none-eabi 跨平台) |
| 烧录 | ST-Link SWD (`build_and_flash.bat`) |
| TFT 显示 | 帧缓冲 + 脏矩形局部刷新，正常 |
| **6 个模式** | Clock / Temp / Timer / Pomodoro / Breathe / Settings |
| 电机 | ✅ A4988 新模块正常运转，EN 空闲断电 |
| 物理指针 | 硬纸片代替，每 30 分钟走一次 |
| ESP32 天气 | ✅ UART DMA 接收，open-meteo 实时数据 |
| SHT30 | 代码就绪，传感器未到 |
| CI | GitHub Actions 5 道 Quality Gate |
| 调试 | OpenOCD + GDB (SWD) + CodeGraph |

---

## 单键操作（6 模式）

| 动作 | 功能 |
|------|------|
| 短按 | 下一模式：Clock → Temp → Timer → Pomodoro → Breathe → Settings → Clock… |
| 长按 (Clock) | 切换 12/24h |
| 长按 (Temp) | 切换天气主页 ↔ 设备信息页 |
| 长按 (Timer) | 开始/暂停/重置倒计时 |
| 长按 (Pomodoro) | 重置番茄钟 |
| 长按 (Breathe) | 退出呼吸引导 |
| 长按 (Settings) | 退出 |

### 各模式画面

| 模式 | TFT 显示 | 物理指针 |
|------|---------|:--:|
| **Clock** | 时间 HH:MM:SS、日期、24h 进度条 | ✅ 每 30 分钟更新 |
| **Temp** | 天气温度+湿度+描述 (ESP32) | - |
| **Timer** | 进度环 + MM:SS + 状态标签 | - |
| **Pomodoro** | 进度环 + 时间 + FOCUS/BREAK + xN | - |
| **Breathe** | 圆圈动画 (INHALE 大→HOLD→EXHALE 小) | - |
| **Settings** | 设置页 | - |

---

## 固件架构

```
main.c
├─ MX_SPI1_Init()     SPI1 @ 10.5MHz (PA5=SCK, PA7=MOSI)
├─ MX_SPI2_Init()     SPI2 @ 2.625MHz (PB10=SCK, PC3=MOSI, PC2=MISO)
├─ MX_USART2_Init()   USART2 @ 115200 (PD6=RX, PD5=ANALOG 关)
├─ MX_TIM2_Init()     TIM2 CH1 PWM (PA0, APB1 84MHz, 83 分频 → 1μs 计数)
├─ HAL_UART_Receive_DMA()  USART2 DMA 循环接收 (256B)
├─ BSP 初始化         st7735, button, a4988, temp_sensor, fs_mgr
└─ FreeRTOS 启动
    ├─ TaskButton (10ms)   按键扫描 → 事件队列
    ├─ TaskBG (50ms)       UART DMA 轮询 + 按键事件 + render → dirty rect flush
    ├─ TaskDisplay (闲置)  帧缓冲模式下无需工作
    └─ TaskMotor (50ms)    启动自检 → 指针引擎 → A4988 (EN 空闲断电)
```

---

## 📌 引脚速查表

| 外设 | 信号 | 引脚 | 板子位置 |
|------|------|:--:|------|
| **TFT** | CS / SCK / MOSI / DC / RST / BL | PA4 / PA5 / PA7 / PC4 / PC5 / PB0 | 下排 |
| **W25Q64** | CS / SCK / MOSI / MISO | PB12 / PB10 / PC3 / PC2 | 下排 |
| **A4988** | STEP / DIR / EN | PA0 / PA1 / PA2 | 下排 |
| **ESP32** | TX→RX | PD6 (USART2_RX) | 上排 D6 |
| **按键** | KEY | PA15 | 上排 A15 |
| **串口** | TX/RX | 板子 RXD/TXD | USB-TTL 交叉接 |

---

## A4988 步进电机模块

### 模块：蓝色底板 + 绿色 A4988 子板，拨码开关版

```
    1 (EN)   2 (STEP)  3 (DIR)   ← 第1排 标 S
    4 (拨码)  5 (拨码)  6 (VDD)   ← 第2排 标 V
    7 (GND)  8 (GND)   9 (GND)   ← 第3排 标 G
```

| 脚位 | 接至 | 说明 |
|:---:|------|------|
| 1 EN | PA2 | 低有效使能，空闲 HIGH 断电 |
| 2 STEP | PA0 | TIM2 CH1 PWM 2μs 脉冲 |
| 3 DIR | PA1 | HIGH=CW, LOW=CCW |
| 6 VDD | 适配器 3.3V | 逻辑供电 |
| 蓝 VIN | 适配器 12V | 电机驱动 |
| 蓝 GND + 9 GND | 适配器 GND | 共地！ |
| 拨码 | ON/ON/ON | 1/16 微步 (3200/圈) |
| Vref | 0.8V | 电位器调节 |

### EN 控制逻辑

| 时刻 | EN | 说明 |
|------|:--:|------|
| 自检期间 | LOW | 电机运转 |
| 自检结束 | HIGH | 释放电机 |
| 正常 diff ≤ 10 | HIGH | 到位断电防过热 |
| 正常 diff > 10 | LOW | 需移动时使能 |

### 电源拓扑

```
适配器 12V  ──► 蓝色端子 VIN            (电机驱动)
适配器 3.3V ──► 6号 VDD                 (A4988 逻辑)
适配器 3.3V ──► TFT VCC                 (TFT 供电，独立于 STM32)
适配器 GND  ──┬──► 蓝色端子 GND
              ├──► 9号 GND
              └──► STM32 GND (共地！)
STM32 ── USB 供电
```

> ⚠️ TFT 不能从 STM32 3.3V 取电（USB 限流掉至 2.5V），改从适配器 3.3V 直供。

---

## 调试

### 三步调试法：CodeGraph → OpenOCD → GDB

```
1. CodeGraph 搜函数名、调用链、外设初始化位置（codegraph_explore）
2. 双击 openocd_debug.bat（启动 OpenOCD :3333 + GDB 连接）
3. GDB 下断点、看寄存器、单步走
```

### GDB 常用命令

| 用途 | 命令 |
|------|------|
| 设断点 | `break <函数名>` 或 `break <文件>:<行号>` |
| 继续运行 | `c` (continue) |
| 单步（不进入） | `n` (next) |
| 单步（进入） | `s` (step) |
| 看调用栈 | `bt` |
| 打变量 | `p <变量名>` |
| 看外设寄存器 | `p/x *(uint32_t*)0x40000000` (TIM2_CR1) |
| 手动调函数 | `p a4988_set_speed(50)` |
| 复位板子 | `monitor reset halt` → `load` |
| 看 TIM2 是否跑 | `p/x *(uint32_t*)0x40000000` → bit0=1 表示在跑 |
| 看 PWM 周期 | `p/d *(uint32_t*)0x4000002C` → ARR 值 (ARR=19999 → 50Hz) |
| 看 PWM 脉宽 | `p/d *(uint32_t*)0x40000034` → CCR1 值 (CCR=2 → 2μs) |
| 看 GPIO 模式 | `p/x *(uint32_t*)0x40020000` → bit1:0=10 表示 AF |

### 电机调试寄存器速查

| 地址 | 寄存器 | 正常值 (50Hz) |
|------|--------|:--:|
| `0x40000000` | TIM2_CR1 | 0x81 (CEN=1 跑) |
| `0x40000028` | TIM2_PSC | 83 |
| `0x4000002C` | TIM2_ARR | 19999 |
| `0x40000034` | TIM2_CCR1 | 2 (2μs 脉宽) |
| `0x40020000` | GPIOA_MODER | PA0=AF (bit1:0=10) |

### 串口日志

USB-TTL 模块交叉接板子 RXD/TXD，115200 baud。关键日志：

- `Motor self-test: CW 50steps/s 2s` — 自检
- `Pointer target set: 220.2 deg (mode 0)` — 指针更新
- `weather frame: 2026-06-29...` — ESP32 天气到达

---

## 编译 & 烧录

| 脚本 | 用途 |
|------|------|
| `build_and_flash.bat` | 编译 + ST-Link SWD 烧录（唯一推荐） |
| `openocd_debug.bat` | OpenOCD + GDB 调试 |
| `mingw32-make -j6` | 命令行编译 |

---

## 已知问题

| # | 优先级 | 问题 | 说明 |
|---|:--:|------|------|
| 1 | P0 | TFT 闪烁 | ST7735S TE 引脚未引出，硬件限制 |
| 2 | P1 | SHT30 未接 | 代码完整，到货插 PF0/PF1 即用 |
| 3 | P2 | Settings 只读 | 单键无方向键 |
| 4 | P3 | RTC 断电丢时间 | VBAT 未接电池，ESP32 NTP 已缓解 |
| 5 | P3 | 编译 Warning | 枚举混用，无害 |

---

## 相关文档

| 文档 | 路径 |
|------|------|
| Bug 索引 (含 15 条记录) | `docs/bugs/INDEX.md` |
| CLAUDE.md | 项目根目录（AI 行为规范） |
| 回归检查 | `.claude/scripts/check-firmware.sh` (18 项) |
| 数据手册 | `C:\Users\wumu2\OneDrive\桌面\stm32F407ZGT6\` |
| ESP32 项目 | `C:\Projects\weather_clock\` |
| CodeGraph | `pointer-desk/.codegraph/` (43MB) |
| jiaocheng.md | CodeGraph + OpenOCD + GDB 教程 |

# pointer-desk

> 物理指针 + TFT 显示屏的混合桌面摆件，ESP32 天气桥接实时数据。

![platform](https://img.shields.io/badge/platform-STM32F407ZGT6-blue)
![rtos](https://img.shields.io/badge/rtos-FreeRTOS-green)
![lang](https://img.shields.io/badge/lang-C11-orange)
![build](https://img.shields.io/badge/build-GCC%20%7C%20ARMCC-brightgreen)
![ci](https://img.shields.io/badge/CI-GitHub%20Actions-2088FF)
[![Quality Gate](https://github.com/wumu-dot/pointer-desk/actions/workflows/quality-gate.yml/badge.svg)](https://github.com/wumu-dot/pointer-desk/actions/workflows/quality-gate.yml)

## 一句话

一块 128×160 TFT 负责渲染界面，一根 42 步进物理指针负责指示位置——时钟、天气、湿度计、番茄钟、呼吸引导，六个模式融合在硬件表盘的视觉里。

## 当前状态

| 模块 | 状态 |
|------|:--:|
| 时钟模式 (12/24h + 日出日落环计划中) | ✅ |
| 天气模式 (ESP32 open-meteo 实时数据) | ✅ |
| 湿度计 (SHT30 本地湿度 + 指针刻度) | ✅ |
| 双温对比 (SHT30本地 vs ESP32区域) | ✅ |
| 倒计时 (进度环 + 状态机) | ✅ |
| 番茄钟 (25分工作 + 5分休息，自动循环) | ✅ |
| 呼吸引导 (4-7-8 圆圈动画) | ✅ |
| 物理指针 (42步进 + A4988) | ✅ 电机正常，每30分钟走一次 |

## 硬件

| 组件 | 接口 |
|------|------|
| STM32F407ZGT6 | 主控 168MHz |
| ST7735S 1.8" TFT | SPI1 (PA5 SCK / PA7 MOSI) |
| W25Q64 8MB Flash | SPI2 |
| 42步进电机 + A4988 | TIM2 (PA0 STEP / PA1 DIR / PA2 EN) |
| SHT30 温湿度 | I2C2 (PF0 SDA / PF1 SCL) |
| ESP32 天气桥接 (weather-clock) | USART2 (PD6 RX) |
| 单按键 | PA15 |

## 工作流

> 三步走的完整开发流程，所有脚本双击即用。

### 1. 编译 — 一键 GCC，无需 Keil

| 脚本 | 用途 | 要求 |
|------|------|------|
| `make -j` | 命令行编译 (跨平台) | `arm-none-eabi-gcc` + `make` |

```
# Ubuntu:     sudo apt install gcc-arm-none-eabi
# macOS:      brew install arm-none-eabi-gcc
# Windows:    STM32CubeCLT 自带，PATH 含 GNU-tools-for-STM32/bin
```

### 2. 烧录 — 两种方式，自动识别

| 脚本 | 方式 | 需要 |
|------|------|------|
| **[build_and_flash.bat](firmware/build_and_flash.bat)** | ST-Link SWD | 插着 ST-Link |
| **[flash.bat](firmware/flash.bat)** | ISP 串口 (USART1) | BOOT0→HIGH→RESET→烧→BOOT0→LOW |

```
ISP 手动步骤（无 ST-Link 时）:
  ① BOOT0 跳到 3.3V
  ② 按 RESET
  ③ 双击 flash.bat
  ④ BOOT0 跳回 GND，按 RESET
```

### 3. 调试 — OpenOCD + GDB

| 脚本 | 调试器 | 端口 |
|------|--------|:---:|
| **[openocd_debug.bat](firmware/openocd_debug.bat)** （推荐） | OpenOCD | 3333 |
| **[gdb_debug.bat](firmware/gdb_debug.bat)** （备用） | ST-Link GDB Server | 7184 |

```
双击 openocd_debug.bat → 自动启动 OpenOCD → GDB 连接 → load → break main
键入 c (continue) 开始运行，Ctrl+C 暂停，bt 看调用栈
```

## CI / Quality Gate

每次 `git push` 触发 GitHub Actions 5 道自动检查：

| Job | 检查内容 |
|-----|---------|
| 静态检查 | 18 项固件回归规则（引脚、SPI 速度、渲染缓存...） |
| 密钥扫描 | 硬编码 API Key / Token / Password |
| 危险函数 | `sprintf` / `strcpy` / `gets` / `scanf` |
| 行数限制 | 项目代码单文件 ≤ 800 行 |
| **GCC 编译** | `arm-none-eabi-gcc` 编译验证 → `.elf` `.hex` `.bin` |

> 不依赖 Keil — Linux/macOS/Windows 均可用 GCC 编译和 CI 验证。

## 文档

| 想看 | 去哪儿 |
|------|--------|
| 接线 + 当前状态 | [HANDOFF.md](HANDOFF.md) |
| 开发规范 | [docs/development-standards.md](docs/development-standards.md) |
| Bug 列表 | [docs/bugs/INDEX.md](docs/bugs/INDEX.md) |
| 功能看板 | [docs/features/INDEX.md](docs/features/INDEX.md) |
| 设计文档 | [docs/superpowers/specs/](docs/superpowers/specs/) |
| 实现计划 | [docs/superpowers/plans/](docs/superpowers/plans/) |
| ESP32 天气源 | [weather-clock](https://github.com/wumu-dot/weather-clock) |

---



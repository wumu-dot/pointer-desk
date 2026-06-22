# pointer-desk

> 物理指针 + TFT 显示屏的混合桌面摆件，ESP32 天气桥接实时数据。

![platform](https://img.shields.io/badge/platform-STM32F407ZGT6-blue)
![rtos](https://img.shields.io/badge/rtos-FreeRTOS-green)
![lang](https://img.shields.io/badge/lang-C11-orange)

## 一句话

一块 128×160 TFT 负责渲染界面，一根 42 步进物理指针负责指示位置——时钟、天气、湿度计、番茄钟、呼吸引导，五个模式融合在硬件表盘的视觉里。

## 当前状态

| 模块 | 状态 |
|------|:--:|
| 时钟模式 (12/24h + 日出日落环计划中) | ✅ |
| 天气模式 (ESP32 open-meteo 实时数据) | ✅ |
| 湿度计 (SHT30 本地湿度 + 指针刻度) | ✅ |
| 双温对比 (SHT30本地 vs ESP32区域) | ✅ |
| 倒计时 / 番茄钟 / 呼吸引导 | ⏳ 计划中 |
| 物理指针 (42步进 + A4988) | ⏳ 待接线验证 |

## 硬件

| 组件 | 接口 |
|------|------|
| STM32F407ZGT6 | 主控 168MHz |
| ST7735S 1.8" TFT | SPI1 (PA5 SCK / PA7 MOSI) |
| W25Q64 8MB Flash | SPI2 |
| 42步进电机 + A4988 | TIM2 (PA0 STEP / PA1 DIR / PA2 EN) |
| SHT30 温湿度 | I2C2 (PF0 SDA / PF1 SCL) |
| ESP32 天气桥接 | USART2 (PD6 RX) |
| 单按键 | PA15 |

## 快速开始

```
1. Keil MDK V5 + ARMCC V5 → 打开 firmware/MDK-ARM/ov-watch.uvprojx
2. Rebuild → 0 Error
3. ST-Link / ISP 串口烧录
4. SSCOM 115200 看日志
```

## 文档

| 想看 | 去哪儿 |
|------|--------|
| 接线 + 当前状态 | [HANDOFF.md](HANDOFF.md) |
| 开发规范 | [docs/development-standards.md](docs/development-standards.md) |
| Bug 列表 | [docs/bugs/INDEX.md](docs/bugs/INDEX.md) |
| 功能看板 | [docs/features/INDEX.md](docs/features/INDEX.md) |
| 设计文档 | [docs/superpowers/specs/](docs/superpowers/specs/) |
| 实现计划 | [docs/superpowers/plans/](docs/superpowers/plans/) |

---



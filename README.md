# OV-Watch 桌面智能摆件

基于 STM32F407ZGT6 + TFT + 物理指针 + ESP32 天气桥接的混合桌面摆件固件。

## 硬件

| 组件 | 接口 |
|------|------|
| STM32F407ZGT6 | 主控 |
| ST7735S 1.8" TFT | SPI1 (PA5/PA7) |
| W25Q64 8MB Flash | SPI2 (PB10/PC3/PC2) |
| 42步进电机 + A4988 | TIM2 (PA0-PA3) |
| SHT30 温湿度 (预留) | I2C2 (PF0/PF1) |
| ESP32 天气桥接 | USART2 (PD6 RX) |
| 单按键 | PA15 |

## 快速开始

1. Keil MDK V5 打开 `firmware/MDK-ARM/ov-watch.uvprojx`
2. ARMCC V5 编译 → Rebuild
3. ISP 串口烧录 (PA9/PA10, BOOT0 拉高)
4. 串口日志 SSCOM 115200 查看

## 文档

| 文档 | 路径 |
|------|------|
| 项目状态 & 接线 | [HANDOFF.md](HANDOFF.md) |
| 开发规范 | [docs/development-standards.md](docs/development-standards.md) |
| Bug 列表 | [docs/bugs/INDEX.md](docs/bugs/INDEX.md) |
| 设计文档 | [docs/superpowers/specs/](docs/superpowers/specs/) |
| 实现计划 | [docs/superpowers/plans/](docs/superpowers/plans/) |
| ESP32 数据源 | `C:\Projects\weather_clock\` |

# OV-Watch 桌面智能摆件

> 基于 STM32F407ZGT6 + TFT + 物理指针的桌面混合智能摆件
>
> 参考 B站UP主"油炸鸡开源硬件"的开源项目 OV-Watch

## 硬件

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | STM32F407ZGT6 | — |
| 屏幕 | 1.8寸 TFT ST7735S | SPI1 |
| 存储 | W25Q64 8MB | SPI2 |
| 电机 | 42步进电机 NEMA17 | A4988 驱动 |
| 输入 | 四角开关 (5向) | GPIO PE0-PE4 |
| 温度 | 内部ADC / SHT30 | ADC1 / I2C2 |
| 时钟 | 内置RTC + CR1220 | VBAT |
| 供电 | 12V DC → AMS1117-3.3V | — |

## 开发环境

- **IDE**: STM32CubeIDE
- **HAL**: STM32Cube FW_F4
- **RTOS**: FreeRTOS (CMSIS-OS v2)
- **文件系统**: LittleFS

## 快速开始

### 1. CubeMX 配置

参考 `docs/cubemx-setup.md` 完成以下配置：
- 引脚分配 (SPI1, SPI2, TIM2, ADC1, I2C2, RTC, GPIO)
- 时钟树 (168MHz, 32.768kHz LSE)
- FreeRTOS (CMSIS-OS v2)
- 各外设参数

### 2. 生成代码

CubeMX 生成代码到 `firmware/` 目录。

### 3. 添加驱动和应用代码

将 Drivers/、Middleware/、App/ 目录下的代码加入工程。

### 4. 编译烧录

```bash
# STM32CubeIDE 中直接 Build 并 Debug
# 或使用命令行
arm-none-eabi-gcc ...
```

## 目录结构

```
ov-watch/
├── docs/
│   ├── cubemx-setup.md          # CubeMX 配置指南
│   └── superpowers/specs/       # 设计文档
├── firmware/
│   ├── Core/                    # CubeMX 生成 + 手动添加
│   │   ├── Inc/                 # 头文件
│   │   └── Src/                 # main.c, 中断等
│   ├── Drivers/
│   │   └── BSP/                 # 板级驱动
│   │       ├── lcd/             # ST7735
│   │       ├── flash/           # W25Q64
│   │       ├── motor/           # A4988
│   │       ├── button/          # 四角开关
│   │       ├── sensor/          # ADC + SHT30
│   │       └── rtc/             # RTC
│   ├── Middleware/
│   │   ├── gui/                 # 轻量GUI
│   │   ├── pointer/             # 指针引擎
│   │   └── fs/                  # LittleFS
│   ├── App/
│   │   ├── modes/               # 四种模式
│   │   ├── tasks/               # FreeRTOS 任务
│   │   └── mode_manager.c/h     # 状态机
│   └── .ioc                     # CubeMX 工程
└── tools/                       # PC 工具
```

## 许可证

GPL-3.0

## 参考

- 原版 OV-Watch: [github.com/No-Chicken/ov-watch](https://github.com/No-Chicken/ov-watch)
- B站教程: BV1hh4y1J7TS

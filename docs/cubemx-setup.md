# CubeMX 配置指南

本文档指导在 STM32CubeMX 中完成 OV-Watch 桌面摆件的硬件配置。

## 1. 新建工程

1. 打开 STM32CubeMX → **File → New Project**
2. 搜索 `STM32F407ZGTx`，选择 LQFP144 封装
3. 工程名: `ov-watch`，保存到 `firmware/`

## 2. 系统核心

### 2.1 Pinout → System Core

| 组件 | 配置项 | 值 |
|------|--------|-----|
| SYS | Debug | Serial Wire (SWD) |
| SYS | Timebase Source | TIM6 |

### 2.2 RCC

| 配置项 | 值 |
|--------|-----|
| High Speed Clock (HSE) | Crystal/Ceramic Resonator |
| Low Speed Clock (LSE) | Crystal/Ceramic Resonator |

### 2.3 时钟树

```
HSE 8MHz → PLL → 168MHz SYSCLK
                    → PLLQ 输出 (本项目未使用USB/SDIO, CubeMX自动锁灰色)
                    → 84MHz  (APB2 Timer)
                    → 42MHz  (APB1 Timer)

LSE 32.768kHz → RTC
```

**Clock Configuration 详细设置：**
1. HSE: 8 MHz (Input)
2. PLL Source Mux: HSE
3. PLL M = 8 → 1MHz at PLL input
4. PLL N = 336 → 336MHz at VCO
5. PLL P = 2 → 168MHz SYSCLK
6. PLL Q = Auto (未使用USB/SDIO/RNG, CubeMX自动设置)
7. AHB Prescaler: 1 → HCLK 168MHz
8. APB1 Prescaler: 4 → APB1 42MHz (TIM 84MHz)
9. APB2 Prescaler: 2 → APB2 84MHz (TIM 168MHz)
10. RTC/LCD Clock: LSE (32.768kHz)

## 3. 外设配置

> CubeMX Pinout 视图中的外设位置导航：
> - **System Core**: SYS, RCC, GPIO, NVIC, DMA
> - **Timers**: TIM2, TIM3, TIM4, TIM5, TIM6, **RTC**, IWDG
> - **Connectivity**: SPI1, SPI2, I2C2, USART1
> - **Analog**: ADC1

### 3.1 SPI1 — TFT LCD (ST7735S)

| 参数 | 值 |
|------|-----|
| Mode | Full-Duplex Master |
| Hardware NSS | Disable (软件控制 CS) |
| Frame Format | Motorola |
| Data Size | 8 Bits |
| First Bit | MSB First |
| Prescaler | 16 (84MHz/16 = 5.25MHz, 面包板可降为 32) |
| CPOL | Low |
| CPHA | 1 Edge |
| CRC | Disable |

**NSS 信号在面包板上如何调整：**
- 面包板飞线多，建议 Prescaler=32 (≈2.625MHz) 起步
- 待 PCB 阶段再改为 8 (10.5MHz)

**GPIO 设置：**
| 信号 | 引脚 | 模式 |
|------|------|------|
| CS | PA4 | Output Push-Pull, High |
| DC | PC4 | Output Push-Pull |
| RST | PC5 | Output Push-Pull |
| BL | PB0 | TIM3_CH3 |

### 3.2 SPI2 — Flash (W25Q64)

| 参数 | 值 |
|------|-----|
| Mode | Full-Duplex Master |
| Hardware NSS | Disable |
| Prescaler | 16 (42MHz/16 ≈ 2.625MHz) |
| CPOL | Low |
| CPHA | 1 Edge |

**GPIO 设置：**
| 信号 | 引脚 | 模式 |
|------|------|------|
| CS | PB12 | Output Push-Pull, High |

### 3.3 TIM2 — 步进电机 STEP 脉冲

| 参数 | 值 |
|------|-----|
| Clock Source | Internal Clock |
| Channel 1 | PWM Generation CH1 |
| Prescaler | 84-1 (84MHz / 84 = 1MHz → 1us tick) |
| Counter Period | 1000-1 (1kHz PWM, 可动态调整频率) |
| Auto-reload preload | Enable |

**GPIO 设置：**
| 信号 | 引脚 | 模式 |
|------|------|------|
| STEP | PA0 | TIM2_CH1, Alternate Function Push-Pull |

### 3.4 TIM3 — TFT 背光 PWM

| 参数 | 值 |
|------|-----|
| Clock Source | Internal Clock |
| Channel 3 | PWM Generation CH3 |
| Prescaler | 84-1 |
| Counter Period | 1000-1 (1kHz PWM, 1000 级亮度) |

### 3.5 TIM4 — 50ms 电机微步基准

| 参数 | 值 |
|------|-----|
| Clock Source | Internal Clock |
| Prescaler | 42000-1 (84MHz / 42000 = 2kHz → 0.5ms tick) |
| Counter Period | 100-1 (100 ticks × 0.5ms = 50ms) |
| NVIC | 使能中断 |

### 3.6 TIM5 — 10ms 按键轮询基准

| 参数 | 值 |
|------|-----|
| Clock Source | Internal Clock |
| Prescaler | 8400-1 (84MHz / 8400 = 10kHz → 0.1ms tick) |
| Counter Period | 100-1 (100 ticks × 0.1ms = 10ms) |
| NVIC | 使能中断 |

### 3.7 ADC1 — 内部温度传感器

| 参数 | 值 |
|------|-----|
| Channel | IN16 (Temperature Sensor Channel) |
| Resolution | 12 bits |
| Sampling Time | 480 Cycles |
| Scan Conversion | Disable |
| Continuous | Disable (软件触发) |

### 3.8 I2C2 — SHT30 预留

| 参数 | 值 |
|------|-----|
| Speed | Standard (100kHz) |
| Addressing | 7-bit |

### 3.9 RTC

| 参数 | 值 |
|------|-----|
| Clock Source | LSE |
| Calendar | Enable |
| Time Format | 24h |
| Date Format | YYYY-MM-DD |
| NVIC | 使能 RTC 全局中断 (可选, 用于闹钟) |

### 3.10 USART1 — 调试串口

| 参数 | 值 |
|------|-----|
| Baud Rate | 115200 |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |
| NVIC | 使能 USART1 全局中断 |

**GPIO 设置：**
| 信号 | 引脚 | 模式 |
|------|------|------|
| TX | PA9 | Alternate Function Push-Pull |
| RX | PA10 | Alternate Function Push-Pull |

### 3.11 VBAT — RTC 后备电池

- **CubeMX 配置**: 无需额外配置（RTC 已在 3.9 节配置完毕）
- **硬件连接**: VBAT 引脚（LQFP144 第 6 脚）→ CR1220 纽扣电池正极 (3V)，负极接地
- **作用**: 主电源断电时自动切换，维持 RTC 走时和备份寄存器内容
- **注意**: 32.768kHz LSE 晶振已在 RCC（2.2 节）配置，与 VBAT 电池是两回事

## 4. GPIO 输出 (手动)

在 Pinout 中设置以下引脚为 **GPIO_Output**：

| 引脚 | 标签 | 初始电平 | 说明 |
|------|------|----------|------|
| PA1 | MOTOR_DIR | Low | A4988 方向 |
| PA2 | MOTOR_EN | High | A4988 使能 (高=禁用) |
| PA3 | MOTOR_MS | High | A4988 细分 (16细分=全高) |

## 5. GPIO 输入 (手动)

在 Pinout 中设置以下引脚为 **GPIO_Input**，内部上拉：

| 引脚 | 标签 | 说明 |
|------|------|------|
| PE0 | BTN_UP | 四角开关 上 |
| PE1 | BTN_DOWN | 四角开关 下 |
| PE2 | BTN_LEFT | 四角开关 左 |
| PE3 | BTN_RIGHT | 四角开关 右 |
| PE4 | BTN_CENTER | 四角开关 中 |

## 6. FreeRTOS 配置

### 6.1 Middleware → FREERTOS

- Interface: **CMSIS_V2**
- Version: 最新稳定版

### 6.2 Tasks and Queues (先创建空任务)

| 任务 | 入口函数 | 栈 | 优先级 |
|------|----------|-----|--------|
| TaskButton | StartTaskButton | 512 | osPriorityHigh |
| TaskBG | StartTaskBG | 2048 | osPriorityNormal |
| TaskDisplay | StartTaskDisplay | 2048 | osPriorityNormal |
| TaskMotor | StartTaskMotor | 1024 | osPriorityAboveNormal |

### 6.3 消息队列

| 队列 | 数量 | 大小 |
|------|------|------|
| QueueBtnEvents | 8 | 4 bytes × 2 (uint16_t×2) |
| QueueRenderCmds | 16 | 4 bytes × 4 (渲染指令结构体) |
| QueueMotorTargets | 4 | 4 bytes × 3 (角度+速度+模式) |

### 6.4 事件组

| 事件组 | 标志位 |
|--------|--------|
| EvtMotor | BIT0: 到位, BIT1: 错误 |

### 6.5 Config Parameters

| 参数 | 值 |
|------|-----|
| TOTAL_HEAP_SIZE | 30720 (30KB) |
| TICK_RATE_HZ | 1000 |
| MAX_PRIORITIES | 56 (CMSIS_V2 固定值, 灰色不可改) |
| MINIMAL_STACK_SIZE | 128 |
| USE_PREEMPTION | Enabled |

## 7. 生成代码

1. **Project Manager** → Project Name: `ov-watch`
2. **Toolchain**: STM32CubeIDE
3. **Code Generator**: 
   - ✅ Generate peripheral initialization as a pair of '.c/.h' files
   - ✅ Keep User Code when re-generating
   - ✅ Delete previously generated files when not re-generated

4. 点击 **GENERATE CODE**
5. 生成完成后，手动添加 `Drivers/BSP/`、`Middleware/`、`App/` 下的代码文件到 IDE

## 8. 验证

生成代码后验证：
- [ ] `MX_SPI1_Init()` 包含正确引脚配置
- [ ] `MX_SPI2_Init()` 无冲突
- [ ] `MX_TIM2_Init()` PWM 输出
- [ ] `MX_ADC1_Init()` 包含温度通道
- [ ] `MX_RTC_Init()` LSE 时钟源
- [ ] FreeRTOS 4个任务 + 3个队列 + 1个事件组
- [ ] 编译通过 (0 errors, 0 warnings)

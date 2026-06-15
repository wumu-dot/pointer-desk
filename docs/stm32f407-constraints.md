# STM32F407 CubeMX 配置约束清单

基于 RM0090 参考手册，用于 CubeMX 配置后的逐项核验。

## 寄存器位宽硬约束

| 约束项 | 限制值 | 适用范围 |
|--------|--------|----------|
| PSC (Prescaler) | ≤ 65535 (16-bit) | **所有定时器** |
| ARR (Counter Period) | ≤ 65535 (16-bit) | TIM1, TIM3, TIM4, TIM6, TIM7, TIM8, TIM9-TIM14 |
| ARR (Counter Period) | ≤ 4294967295 (32-bit) | TIM2, TIM5 |

> ARR = Counter Period + 1，CubeMX 界面直接填 Counter Period 值

## 时钟约束

| 约束项 | 限制值 |
|--------|--------|
| PLL VCO | 192-432 MHz |
| PLL 输入 (HSE/M) | 0.95-2.1 MHz |
| SYSCLK max | 168 MHz |
| APB1 max | 42 MHz |
| APB2 max | 84 MHz |
| ADC clock max | 36 MHz |

## 外设时钟源速查

| 外设 | 挂载总线 | 时钟来源 | 当前值 |
|------|----------|----------|--------|
| SPI1 | APB2 | APB2 外设时钟 | 84 MHz |
| SPI2 | APB1 | APB1 外设时钟 | 42 MHz |
| I2C2 | APB1 | APB1 外设时钟 | 42 MHz |
| USART1 | APB2 | APB2 外设时钟 | 84 MHz |
| TIM1 | APB2 | APB2 定时器时钟 | 168 MHz |
| TIM2 | APB1 | APB1 定时器时钟 | 84 MHz |
| TIM3 | APB1 | APB1 定时器时钟 | 84 MHz |
| TIM4 | APB1 | APB1 定时器时钟 | 84 MHz |
| TIM5 | APB1 | APB1 定时器时钟 | 84 MHz |
| TIM6 | APB1 | APB1 定时器时钟 | 84 MHz |

> 规则：定时器时钟 = 2 × APBx 外设时钟（当 APBx Prescaler ≠ 1 时）

## 外设特定约束

### ADC1
| 约束项 | 值 |
|--------|-----|
| 内部温度传感器通道 | **IN16** (不是 IN10) |
| 内部参考电压通道 | IN17 |
| 采样时间 (温度传感器) | ≥ 10μs，推荐 480 Cycles @ 21MHz ADC 时钟 |
| ADC 时钟分频 | 推荐 /4 (84/4=21MHz) |

### SPI
| 约束项 | 公式 |
|--------|------|
| SCK 频率 | APB外设时钟 / Prescaler |
| Prescaler 取值 | 2, 4, 8, 16, 32, 64, 128, 256 |

### I2C
| 约束项 | 限制 |
|--------|------|
| Standard mode | 100 kHz |
| Fast mode | 400 kHz |
| SHT30 地址 | 0x44 (ADDR=GND) 或 0x45 (ADDR=VDD) |

## 定时器溢出频率校验公式

$$\text{溢出频率} = \frac{\text{定时器时钟}}{(\text{PSC}+1) \times (\text{ARR}+1)}$$

## 当前项目定时器配置校验

| 定时器 | 位宽 | PSC | ARR | 计算 | 溢出频率 | 用途 |
|--------|------|-----|-----|------|----------|------|
| TIM2 | 32 | 83 | 999 | 84M/(84×1000) | 1 kHz | 电机 STEP PWM |
| TIM3 | 16 | 83 | 999 | 84M/(84×1000) | 1 kHz | 背光 PWM |
| TIM4 | 16 | 41999 | 99 | 84M/(42000×100) | 20 Hz (50ms) | 电机微步基准 |
| TIM5 | 32 | 8399 | 99 | 84M/(8400×100) | 100 Hz (10ms) | 按键轮询基准 |

## 引脚冲突检查

| 引脚 | 功能 | AF | 备注 |
|------|------|-----|------|
| PA0 | TIM2_CH1 (STEP) | AF1 | — |
| PA1 | GPIO OUT (MOTOR_DIR) | — | — |
| PA2 | GPIO OUT (MOTOR_EN) | — | — |
| PA3 | GPIO OUT (MOTOR_MS) | — | — |
| PA4 | GPIO OUT (SPI1_CS) | — | 软件 CS |
| PA5 | SPI1_SCK | AF5 | 默认 |
| PA6 | SPI1_MISO | AF5 | 默认 |
| PA7 | SPI1_MOSI | AF5 | 默认 |
| PA9 | USART1_TX | AF7 | — |
| PA10 | USART1_RX | AF7 | — |
| PB0 | TIM3_CH3 (BL) | AF2 | — |
| PB10 | I2C2_SCL | AF4 | SHT30 预留 |
| PB11 | I2C2_SDA | AF4 | SHT30 预留 |
| PB12 | GPIO OUT (SPI2_CS) | — | 软件 CS |
| PB13 | SPI2_SCK | AF5 | 默认 |
| PB14 | SPI2_MISO | AF5 | 默认 |
| PB15 | SPI2_MOSI | AF5 | 默认 |
| PC0 | ADC1_IN10 | — | 未使用 |
| PC4 | GPIO OUT (DC) | — | TFT 控制 |
| PC5 | GPIO OUT (RST) | — | TFT 控制 |
| PE0 | GPIO IN (BTN_UP) | — | 内部上拉 |
| PE1 | GPIO IN (BTN_DOWN) | — | 内部上拉 |
| PE2 | GPIO IN (BTN_LEFT) | — | 内部上拉 |
| PE3 | GPIO IN (BTN_RIGHT) | — | 内部上拉 |
| PE4 | GPIO IN (BTN_CENTER) | — | 内部上拉 |

> ✅ 当前引脚分配无冲突

## 常见错误速查

- [ ] **PSC 溢出**: 所有定时器的 PSC 值 +1 ≤ 65536
- [ ] **ARR 溢出**: TIM3/TIM4/TIM6 的 ARR 值 +1 ≤ 65536
- [ ] **SPI 时钟源**: 不是定时器时钟，是 APB 外设时钟
- [ ] **ADC 温度通道**: IN16，不是 IN10
- [ ] **ADC 时钟**: 不超过 36MHz
- [ ] **FreeRTOS TICK_RATE_HZ**: 1000 (与 SysTick 一致)
- [ ] **NVIC 中断**: TIM4, TIM5, USART1 已使能

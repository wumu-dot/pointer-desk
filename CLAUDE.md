# 项目地图

> 本文档始终加载到 AI 上下文。保持精简（< 1000 Token），只写"是什么+去哪查"，不做完整手册。

---

## 技术栈

STM32F407ZGT6 / Keil MDK V5 + ARMCC V5 / FreeRTOS (CMSIS-RTOS v2) / ST7735S TFT / W25Q64 Flash / ESP32 (ESP-IDF v5.4) / C

---

## 目录速查

| 目录 | 职责 |
|------|------|
| `firmware/Core/` | STM32 HAL 层（main.c, 中断, CubeMX 配置） |
| `firmware/App/` | 应用层（weather_bridge, mode_manager, 4 模式） |
| `firmware/Drivers/BSP/` | 板级驱动（st7735, a4988, temp_sensor, w25q64） |
| `firmware/Middleware/` | 中间件（fs_mgr Flash 存储, gui 绘图, pointer_engine 指针引擎） |
| `firmware/MDK-ARM/` | Keil 项目文件 + 编译产物（.hex 在此） |
| `.claude/scripts/` | 回归检查脚本 |
| `docs/superpowers/` | 设计文档 + 实现计划 |

---

## 入口 & 关键文件

| 文件 | 职责 |
|------|------|
| `firmware/Core/Src/main.c` | 固件入口，外设初始化，FreeRTOS 启动 |
| `firmware/Core/Src/stm32f4xx_hal_msp.c` | CubeMX 生成的引脚配置（**改引脚前必读**） |
| `firmware/Core/Inc/pin_config.h` | 所有引脚宏定义 |
| `firmware/Core/Inc/app_config.h` | 应用配置宏 + weather_data_t 结构体 |
| `firmware/App/weather_bridge.c` | ESP32 UART DMA 接收 + 帧解析 |
| `firmware/App/mode_manager.c` | 模式状态机（6 模式）+ 按键分发 |
| `firmware/App/modes/clock_mode.c` | 时钟模式（指针半小时更新） |
| `firmware/App/modes/temp_mode.c` | 天气/温度模式 |
| `firmware/App/modes/timer_mode.c` | 倒计时模式（进度环 + TFT 闪烁） |
| `firmware/App/modes/pomodoro_mode.c` | 番茄钟模式（25+5分自动循环） |
| `firmware/App/modes/breathe_mode.c` | 呼吸引导模式（4-7-8 圆圈动画） |
| `firmware/App/modes/settings_mode.c` | 设置模式 |
| `firmware/Middleware/fs/fs_mgr.c` | Flash KV 存储 |
| `firmware/Middleware/gui/gui.c` | 轻量GUI（脏矩形、进度环、文本） |
| `firmware/Middleware/pointer/pointer_engine.c` | 指针引擎（角度映射、平滑插值） |
| `firmware/Drivers/BSP/motor/a4988.c` | A4988 步进电机驱动（TIM2 PWM） |
| `firmware/Drivers/BSP/sensor/temp_sensor.c` | 温度传感器抽象（SHT30 + ADC 回退） |
| `C:\Projects\weather_clock\` | ESP32 天气数据源项目 |
| `C:\Projects\weather_clock\main\tasks\uart_bridge_task.c` | ESP32 UART 发送任务 |

---

## 约束 & 禁止事项

### 编译环境
- **STM32CubeCLT (GCC)**（命令行，**推荐**，跨平台）
- **Keil MDK V5 + ARMCC V5**（GUI，兼容保留）
- 编译命令：`mingw32-make -j6`（Windows）/ `make -j$(nproc)`（Linux/Mac）
- ARMCC V5 **不支持** `0b` 二进制字面量、C++ 风格注释混用
- 编译完运行 `bash .claude/scripts/check-firmware.sh` 验证 18 项检查

### 烧录方式
| 脚本 | 方式 | 说明 |
|------|------|------|
| `build_and_flash.bat` | ST-Link SWD | **唯一可用**，一键编译+烧录 |
| `flash.bat` | ISP 串口 (USART1) | ⚠️ 需 PA9/PA10 接 USB-TTL（当前未接） |

- 串口日志：USART1 115200，通过 USB-TTL 模块交叉接板子 RXD/TXD

### 调试
| 脚本 | 调试器 | 端口 |
|------|--------|:---:|
| `openocd_debug.bat`（推荐） | OpenOCD + GDB | 3333 |
| `gdb_debug.bat`（备用） | ST-Link GDB Server | 7184 |

### 引脚规则
- **修改任何引脚前必须先看 `Core/Src/stm32f4xx_hal_msp.c`**
- `pin_config.h` 的宏值必须与 `hal_msp.c` 一致
- 历史上 5 个引脚因未校验 CubeMX 而写错
- **A4988 EN → PA2**（低有效使能，空闲高禁用防过热）；MS1-3 由模块拨码开关控制

### 外设总线
- SPI1 (TFT)：APB2 84MHz ÷ 8 = 10.5MHz
- SPI2 (Flash)：APB1 42MHz ÷ 16 = 2.625MHz
- I2C2 (SHT30)：PF0/PF1，非 PB10/PB11

### 代码原则
- 做最小可能的改动，不重构无关代码
- 保持向后兼容，永不删除现有功能
- 函数前加注释说明功能、参数、返回值

### Git & CI 工作流
- **本地编译先过，再 push**：`mingw32-make -j6` 0 Error → 再 `git push`
- **完成一个小阶段就 push，不要攒**：加完一个功能/修完一个 bug → push，不等 CI 不回头堆
- **不要每次对话都 push**：每句话 push 没用，每个阶段 push 一次
- **CI 是自动的**：push 后 GitHub Actions 自动跑，不需要额外操作
- 公开仓库 CI 不限分钟数，不用担心配额
- CI 红了立刻修，不带着红色 badge 继续干活

### GDB 调试
- 环境已配好：`openocd_debug.bat`（OpenOCD + GDB，端口 3333）
- 能下断点、看寄存器、查 HardFault 调用栈
- 硬件问题（供电/Vref/接线）优先万用表；代码逻辑问题优先 GDB
- 优先用 GDB 而非串口日志排查单步问题

### A4988 电机
- EN → PA2，低有效使能，空闲 HIGH 断电防过热
- 自检结束后 motor task 进入位置伺服：只有 diff > 10 步才使能运转
- 时钟指针每 30 分钟更新一次角度（bucket 法），减少电机微动
- 物理指针：硬纸片代替，等机械结构就位后可替换

---

## 规范索引

> 当你需要执行以下操作时，用 `Read` 工具按路径读取对应章节，不要凭记忆猜测。

| 场景 | 读取命令 |
|------|---------|
| 开始改代码 | `Read("CLAUDE.md")` — 本文档（约束 & 禁止事项） |
| 实现新功能/多文件改动 | 调 `brainstorming` Skill 讨论设计 |
| 写实现计划 | 调 `writing-plans` Skill |
| 遇到 Bug | 调 `systematic-debugging` Skill |
| 写完代码/修完 Bug 后验证 | 调 `verification-before-completion` Skill |
| Git 提交 | 调 `commit-commands:commit` Skill |
| 提交并推送 | 调 `commit-commands:commit-push-pr` Skill |
| 修复Bug后记录 | `Read("docs/development-standards.md", offset=Bug记录规范章节)` |
| 查项目全貌/调试帮助 | `Read("HANDOFF.md")` |
| 查已知 Bug | `Read("docs/bugs/INDEX.md")` 再按需读具体文件 |
| 查 4 功能实施计划 | `Read("docs/superpowers/plans/2026-06-17-motor-4-features.md")` |
| 查 ESP32 端架构 | `Read("C:\Projects\weather_clock\README.md")` |

---

## 外部依赖

- ESP32 项目：`C:\Projects\weather_clock\`（ESP-IDF v5.4）
- 外部 API：open-meteo（ESP32 端，免费天气数据）
- 数据手册：`C:\Users\wumu2\OneDrive\桌面\stm32F407ZGT6\`
  - `ST7735S_datasheet.pdf` — TFT 液晶驱动
  - `A4988_datasheet.pdf` — 步进电机驱动 (Allegro)
- 回归检查：`.claude/scripts/check-firmware.sh`（18 项）
- Ai 开发规范：`C:\Users\wumu2\OneDrive\桌面\agent\al开发项目规范\`

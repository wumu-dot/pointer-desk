# 🚀 OV-Watch 固件实现 — 快速上手

> 打开这个文件就可以直接开始写代码，不需要重新理解项目。

---

## 📋 一句话目标

给 OV-Watch 桌面智能摆件写固件 — **15 个 .c 文件 + main.c/freertos.c 集成**。

所有 .h 头文件已定义好接口，只需要对着写实现。

---

## 🗺️ 实现顺序（严格自底向上）

```
phase1  BSP驱动(6个) → phase2 中间件(3个) → phase3 应用层(5个) → phase4 RTOS集成
```

---

## ⚡ Phase 1: BSP 驱动层（无依赖，可直接开始）

### Task 1 — st7735.c
📁 新建: `firmware/Drivers/BSP/lcd/st7735.c`
📖 接口: `firmware/Drivers/BSP/lcd/st7735.h`
🔗 参考: `firmware/Core/Inc/pin_config.h` (引脚宏), `firmware/Core/Inc/main.h` (HAL句柄)

### Task 2 — w25q64.c
📁 新建: `firmware/Drivers/BSP/flash/w25q64.c`
📖 接口: `firmware/Drivers/BSP/flash/w25q64.h`

### Task 3 — a4988.c
📁 新建: `firmware/Drivers/BSP/motor/a4988.c`
📖 接口: `firmware/Drivers/BSP/motor/a4988.h`

### Task 4 — button.c
📁 新建: `firmware/Drivers/BSP/button/button.c`
📖 接口: `firmware/Drivers/BSP/button/button.h`

### Task 5 — temp_sensor.c + temp_adc.c
📁 新建: `firmware/Drivers/BSP/sensor/temp_adc.c`
📁 新建: `firmware/Drivers/BSP/sensor/temp_sensor.c`
📖 接口: `firmware/Drivers/BSP/sensor/temp_sensor.h`

### Task 6 — rtc_drv.c
📁 新建: `firmware/Drivers/BSP/rtc/rtc_drv.c`
📖 接口: `firmware/Drivers/BSP/rtc/rtc_drv.h`

---

## ⚡ Phase 2: 中间件层（依赖 BSP）

### Task 7 — gui.c
📁 新建: `firmware/Middleware/gui/gui.c`
📖 接口: `firmware/Middleware/gui/gui.h`

### Task 8 — pointer_engine.c
📁 新建: `firmware/Middleware/pointer/pointer_engine.c`
📖 接口: `firmware/Middleware/pointer/pointer_engine.h`

### Task 9 — fs_mgr.c
📁 新建: `firmware/Middleware/fs/fs_mgr.c`
📖 接口: `firmware/Middleware/fs/fs_mgr.h`
⚠️ 依赖 LittleFS 库，需确认 `Middlewares/Third_Party/LittleFS/` 存在

---

## ⚡ Phase 3: 应用层（依赖中间件）

### Task 10 — clock_mode.c
📁 新建: `firmware/App/modes/clock_mode.c`

### Task 11 — temp_mode.c
📁 新建: `firmware/App/modes/temp_mode.c`

### Task 12 — timer_mode.c
📁 新建: `firmware/App/modes/timer_mode.c`

### Task 13 — settings_mode.c
📁 新建: `firmware/App/modes/settings_mode.c`

### Task 14 — mode_manager.c
📁 新建: `firmware/App/mode_manager.c`

---

## ⚡ Phase 4: RTOS 集成（最后一步）

### Task 15 — 填入 main.c + freertos.c 的 USER CODE 块

📁 修改: `firmware/Core/Src/main.c`
📁 修改: `firmware/Core/Src/freertos.c`

---

## 📖 关键参考文件

| 文件 | 作用 |
|------|------|
| [设计文档](docs/superpowers/specs/2026-06-10-ov-watch-design.md) | 系统架构、引脚、模式功能详述 |
| [详细实现计划](docs/superpowers/plans/2026-06-12-ov-watch-firmware.md) | **每个文件的完整 C 代码都在这里面** |
| [引脚配置](firmware/Core/Inc/pin_config.h) | 所有引脚宏定义 |
| [应用配置](firmware/Core/Inc/app_config.h) | 可调参数 + LOG 宏 |
| [main.h](firmware/Core/Inc/main.h) | HAL 外设句柄 |
| [FreeRTOSConfig.h](firmware/Core/Inc/FreeRTOSConfig.h) | RTOS 参数 |

---

## 🔑 关键约定

1. **所有 HAL 外设句柄**（hspi1, htim2, hadc1...）在 `main.h` 中已 extern 声明，直接 `#include "main.h"` 即可使用
2. **所有引脚宏** 在 `pin_config.h` 中，用 `LCD_CS_PORT`/`LCD_CS_PIN` 等，不要硬编码 GPIO
3. **调试日志** 用 `LOG()` / `LOG_ERR()` 宏（在 `app_config.h` 中定义），DEBUG_ENABLED=0 时自动去除
4. **CubeMX USER CODE 块** 在 `/* USER CODE BEGIN Xxx */` 和 `/* USER CODE END Xxx */` 之间写代码，CubeMX 重新生成时不会覆盖
5. **编译环境**: arm-none-eabi-gcc + `-lm` (数学库) + STM32F407xx 宏

---

## ✅ 每完成一个 Task 后

```bash
# 在 STM32CubeIDE 中 Build，或命令行：
# 确认 0 error，warning 可忽略
```

---

## 📞 需要帮助时

对 Claude 说: "按照 `docs/superpowers/plans/2026-06-12-ov-watch-firmware.md` 中的 Task N 写代码"

详细代码模板都在那个计划文件里，每个 Task 都有完整可复制的 C 代码。

# OV-Watch 固件编译问题修复记录

> 日期：2026-06-12
> 修复范围：全部 15 个新 .c 文件 + 2 个修改文件的编译一致性审查

---

## 问题汇总

| # | 严重性 | 涉及文件 | 行数 | 类型 |
|---|--------|----------|------|------|
| 1 | ❌ ERROR | `App/modes/temp_mode.c` | L28, L95 | 函数调用参数缺失 |
| 2 | ❌ ERROR | `App/modes/settings_mode.c` | 22 处 | 函数调用参数缺失 |
| 3 | ❌ ERROR | `App/modes/temp_mode.c` | L1 | 缺少 `#include` |
| 4 | ❌ ERROR | `App/tasks/tasks.h` | L23-26 | extern 声明符号名不匹配 |
| 5 | ⚠️ WARNING | `Core/Inc/pin_config.h` | L122 | 宏值与 CubeMX 生成代码不一致 |

---

## 问题 1: `gui_dirty_mark()` 无参调用 — temp_mode.c

**严重性:** ❌ ERROR (编译失败)

**原因:** `gui_dirty_mark()` 在 [gui.h](../../firmware/Middleware/gui/gui.h#L26) 中声明需要 4 个参数：
```c
void gui_dirty_mark(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
```
但 temp_mode.c 中两处调用了无参版本，参数数量不匹配。

**涉及位置:**
- [temp_mode.c:28](../../firmware/App/modes/temp_mode.c#L28) — `temp_mode_enter()` 中
- [temp_mode.c:95](../../firmware/App/modes/temp_mode.c#L95) — `temp_mode_handle_button()` 中

**修复:**
```c
// 修复前
gui_dirty_mark();

// 修复后
gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
```

**涉及文件:** `firmware/App/modes/temp_mode.c`

---

## 问题 2: `gui_dirty_mark()` 无参调用 — settings_mode.c

**严重性:** ❌ ERROR (编译失败)

**原因:** 与问题 1 相同，settings_mode.c 中有 22 处 `gui_dirty_mark()` 无参调用。

**涉及位置:** [settings_mode.c](../../firmware/App/modes/settings_mode.c#L99) 及后续 21 处:
- L99 — `settings_mode_enter()`
- L329, L337, L345, L354, L361 — main menu 导航
- L405, L414, L423 — brightness 子菜单
- L429, L444, L457, L465, L471 — datetime 子菜单
- L487, L495, L504, L510 — 其他子菜单
- L529, L535, L550 — factory reset / about

**修复:** 全局替换
```c
// 修复前 (22处)
gui_dirty_mark();

// 修复后 (22处)
gui_dirty_mark(0, 0, LCD_WIDTH, LCD_HEIGHT);
```

**涉及文件:** `firmware/App/modes/settings_mode.c`

---

## 问题 3: temp_mode.c 缺少 `main.h`

**严重性:** ❌ ERROR (编译失败)

**原因:** [temp_mode.c](../../firmware/App/modes/temp_mode.c) 中使用了 `HAL_GetTick()`（在 L34），但 `#include` 列表中缺少 `"main.h"`。虽然 `stm32f4xx_hal.h` 可以通过其他头文件间接包含进来，但 `HAL_GetTick()` 是在 HAL 驱动层定义的，直接包含 `main.h` 是项目的约定做法。

**修复:**
```c
// 修复前
#include "temp_mode.h"
#include "temp_sensor.h"
#include "gui.h"
#include "pointer_engine.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include <stdio.h>

// 修复后
#include "temp_mode.h"
#include "temp_sensor.h"
#include "gui.h"
#include "pointer_engine.h"
#include "st7735.h"
#include "pin_config.h"
#include "app_config.h"
#include "button.h"
#include "main.h"          // ← 新增
#include <stdio.h>
```

**涉及文件:** `firmware/App/modes/temp_mode.c`

---

## 问题 4: tasks.h extern 句柄名不匹配

**严重性:** ❌ ERROR (链接失败 — 符号未定义)

**原因:** [tasks.h](../../firmware/App/tasks/tasks.h#L23-L26) 中声明的 FreeRTOS 句柄名与 [main.c](../../firmware/Core/Src/main.c) 中 CubeMX 生成的实际变量名不一致。CubeMX 生成的变量名带有 `Handle` 后缀。

```c
// main.c (CubeMX 生成, 带 Handle 后缀)
osMessageQueueId_t QueueBtnEventsHandle;
osMessageQueueId_t QueueRenderCmdsHandle;
osMessageQueueId_t QueueMotorTargetsHandle;
osEventFlagsId_t   EvtMotorHandle;

// tasks.h (修复前, 缺少 Handle 后缀)
extern osMessageQueueId_t QueueBtnEvents;
extern osMessageQueueId_t QueueRenderCmds;
extern osMessageQueueId_t QueueMotorTargets;
extern osEventFlagsId_t   EvtMotor;
```

这会导致链接器报错 `undefined reference to QueueBtnEvents` 等，因为 `freertos.c` 中使用的是带后缀的名称（来自 main.c 的定义），而 tasks.h 声明的是不带后缀的名称。

**修复:**
```c
// 修复前
extern osMessageQueueId_t QueueBtnEvents;
extern osMessageQueueId_t QueueRenderCmds;
extern osMessageQueueId_t QueueMotorTargets;
extern osEventFlagsId_t   EvtMotor;

// 修复后
extern osMessageQueueId_t QueueBtnEventsHandle;
extern osMessageQueueId_t QueueRenderCmdsHandle;
extern osMessageQueueId_t QueueMotorTargetsHandle;
extern osEventFlagsId_t   EvtMotorHandle;
```

**涉及文件:** `firmware/App/tasks/tasks.h`

---

## 问题 5: TEMP_ADC_CHANNEL 宏值不一致

**严重性:** ⚠️ WARNING (可编译，但 ADC 读数错误)

**原因:** [pin_config.h](../../firmware/Core/Inc/pin_config.h#L122) 中将温度传感器 ADC 通道定义为 `ADC_CHANNEL_10`，但 CubeMX 生成的 [main.c](../../firmware/Core/Src/main.c) 中使用的是 `ADC_CHANNEL_TEMPSENSOR`。两者在 STM32F4 HAL 中定义不同：

- `ADC_CHANNEL_10` = 10 — 对应 PC0 引脚上的 ADC1_IN10（普通 ADC 输入通道）
- `ADC_CHANNEL_TEMPSENSOR` = 16 — 对应内部温度传感器通道（ADC1 内部通道）

使用 `ADC_CHANNEL_10` 会导致 temp_adc.c 读取 PC0 引脚的模拟电压而非内部温度传感器，温度读数完全错误。

**修复:**
```c
// 修复前
#define TEMP_ADC_CHANNEL    ADC_CHANNEL_10

// 修复后
#define TEMP_ADC_CHANNEL    ADC_CHANNEL_TEMPSENSOR
```

**涉及文件:** `firmware/Core/Inc/pin_config.h`

**设计文档对应:** [2026-06-10-ov-watch-design.md 第 277 行](../specs/2026-06-10-ov-watch-design.md#L277) 标注 `PC0 | ADC1_CH10 | 内部温度传感器`，但 STM32F4 的内部温度传感器通道编号是 16（`ADC_CHANNEL_TEMPSENSOR`），而非通道 10。PC0 对应的是 ADC1 外部通道 10，不是内部温度传感器。

---

## 修复统计

| 文件 | 修改次数 | 类型 |
|------|----------|------|
| `firmware/App/modes/temp_mode.c` | 3 | 新增 include + 2处参数修复 |
| `firmware/App/modes/settings_mode.c` | 1 (replace_all 22处) | 参数修复 |
| `firmware/App/tasks/tasks.h` | 1 (4个符号) | 符号名修正 |
| `firmware/Core/Inc/pin_config.h` | 1 | 宏值修正 |
| **合计** | **4 文件, 5 类问题** | |

---

## 编译前置条件

要成功编译，还需要确保以下第三方库已添加到工程 include path：
- [ ] LittleFS 源码 (`lfs.h`, `lfs.c`) — 目前 fs_mgr.c 使用简易模拟层，暂不需要
- [ ] 数学库 `-lm` 链接选项 — gui.c 和 pointer_engine.c 使用 `cosf()`/`sinf()`/`fabsf()`
- [ ] CMSIS-RTOS2 头文件路径 — `cmsis_os2.h`

所有 15 个新 .c 文件需添加到 Keil MDK (`ov-watch.uvprojx`) 或 STM32CubeIDE 工程中。

---

## 第二轮：Keil MDK (ARMCC V5) 编译错误修复

> 编译器: ARM Compiler V5.06 update 6 (build 750)
> 初始结果: 40 Error(s), 9 Warning(s)
> 修复后: 0 Error(s), 4 Warning(s)（警告为多字节字符编码提示，不影响编译）

---

### 问题汇总 (第二轮)

| # | 严重性 | 错误数 | 涉及文件 | 类型 |
|---|--------|--------|----------|------|
| 6 | ❌ ERROR | 10 | `clock_mode.h`, `temp_mode.h`, `timer_mode.h`, `settings_mode.h` | `button_id_t`/`button_event_t` 未定义 |
| 7 | ❌ ERROR | 30 | `Middleware/gui/gui.c` L442-537 | ARMCC 不支持 `0b` 二进制字面量 |
| 8 | ❌ ERROR | 1 | `Drivers/BSP/sensor/temp_adc.c` L88 | `ADC_REGULAR_RANK_1` 宏不存在 |
| 9 | ❌ ERROR | 1 | `Drivers/BSP/motor/a4988.c` L225 | STM32F4 没有 `BRR` 寄存器 |
| 10 | ❌ ERROR | 1 | `Drivers/BSP/motor/a4988.c` L187 | `__HAL_TIM_GENERATE_EVENT` 隐式声明 |
| 11 | ⚠️ WARNING | 1 | `Drivers/BSP/lcd/st7735.c` L510 | 变量 `base_w` 设置但未使用 |
| 12 | ⚠️ WARNING | 1 | `Drivers/BSP/flash/w25q64.c` L166 | `w25q64_read_status2` 声明但未引用 |
| 13 | ⚠️ WARNING | 1 | `App/modes/timer_mode.c` L39 | 变量 `finish_tick` 设置但未使用 |

---

### 问题 6: 模式头文件缺少 `#include "button.h"`

**严重性:** ❌ ERROR (编译失败, 10 errors)

**错误信息:**
```
../App/modes/clock_mode.h(15): error: #20: identifier "button_id_t" is undefined
../App/modes/temp_mode.h(15): error: #20: identifier "button_id_t" is undefined
../App/modes/timer_mode.h(16): error: #20: identifier "button_id_t" is undefined
../App/modes/settings_mode.h(16): error: #20: identifier "button_id_t" is undefined
```

**原因:** 4 个模式头文件的 `handle_button` 函数声明都使用了 `button_id_t` 和 `button_event_t` 类型，但都没有 `#include "button.h"`。在第一轮审查时这些头文件中的类型通过 `.c` 文件的 include 顺序间接获得，但在 Keil 中头文件被独立解析时暴露了依赖缺失。

**修复:** 在 4 个 `.h` 文件中各添加 `#include "button.h"`：

| 文件 | 修改 |
|------|------|
| [clock_mode.h](../../firmware/App/modes/clock_mode.h#L9) | `#include "button.h"` |
| [temp_mode.h](../../firmware/App/modes/temp_mode.h#L9) | `#include "button.h"` |
| [timer_mode.h](../../firmware/App/modes/timer_mode.h#L10) | `#include "button.h"` |
| [settings_mode.h](../../firmware/App/modes/settings_mode.h#L10) | `#include "button.h"` |

---

### 问题 7: ARMCC 不支持 `0b` 二进制字面量

**严重性:** ❌ ERROR (编译失败, 30 errors)

**错误信息:**
```
../Middleware/gui/gui.c(445): error: #253: expected a ","
../Middleware/gui/gui.c(446): error: #253: expected a ","
... (重复 30 次)
```

**原因:** [gui.c](../../firmware/Middleware/gui/gui.c#L442-L537) 的 `icon_bitmaps` 数组使用了 C23/GCC 扩展语法 `0bXXXXXXXX` 二进制字面量，ARM Compiler V5 (基于 EDG 前端) 不支持此语法。

**修复:** 全部 64 个字节从二进制转换为十六进制：

```c
// 修复前 (ARMCC 不支持)
static const uint8_t icon_bitmaps[][8] = {
    /* ICON_SUN */
    {
        0b00100100, 0b00011000, 0b01111110, 0b01111110,
        0b01111110, 0b01111110, 0b00011000, 0b00100100,
    },
    // ... 7 more icons, 30 errors total
};

// 修复后
static const uint8_t icon_bitmaps[][8] = {
    /* ICON_SUN */
    {0x24, 0x18, 0x7E, 0x7E, 0x7E, 0x7E, 0x18, 0x24},
    /* ICON_MOON */
    {0x3C, 0x78, 0x70, 0x60, 0x60, 0x70, 0x78, 0x3C},
    /* ICON_ARROW_UP */
    {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* ICON_ARROW_DOWN */
    {0x00, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18},
    /* ICON_ARROW_LEFT */
    {0x00, 0x08, 0x18, 0x3E, 0x3E, 0x18, 0x08, 0x00},
    /* ICON_ARROW_RIGHT */
    {0x00, 0x10, 0x18, 0x7C, 0x7C, 0x18, 0x10, 0x00},
    /* ICON_CHECK */
    {0x01, 0x03, 0x06, 0x0C, 0x58, 0x70, 0x20, 0x00},
    /* ICON_CROSS */
    {0x41, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x41},
};
```

**涉及文件:** `firmware/Middleware/gui/gui.c`

---

### 问题 8: `ADC_REGULAR_RANK_1` 宏不存在

**严重性:** ❌ ERROR (编译失败, 1 error)

**错误信息:**
```
../Drivers/BSP/sensor/temp_adc.c(88): error: #20: identifier "ADC_REGULAR_RANK_1" is undefined
```

**原因:** STM32F4 HAL 库的 `ADC_ChannelConfTypeDef.Rank` 字段使用数值 `1` 而非宏 `ADC_REGULAR_RANK_1`。此宏在 STM32H7 系列才存在，F4 系列没有定义。

**修复:**
```c
// 修复前
sConfig.Rank = ADC_REGULAR_RANK_1;

// 修复后
sConfig.Rank = 1;
```

**涉及文件:** `firmware/Drivers/BSP/sensor/temp_adc.c` L88

---

### 问题 9: STM32F4 无 `BRR` 寄存器

**严重性:** ❌ ERROR (编译失败, 1 error)

**错误信息:**
```
../Drivers/BSP/motor/a4988.c(225): error: #136: struct "<unnamed>" has no field "BRR"
```

**原因:** STM32F4 的 `GPIO_TypeDef` 结构体没有 `BRR`（Bit Reset Register）成员。`BRR` 存在于 STM32F1 系列，F4 使用 `BSRR` 的高 16 位来实现同样的复位功能。

**修复:**
```c
// 修复前
MOTOR_STEP_PORT->BRR = MOTOR_STEP_PIN;  /* STEP = LOW */

// 修复后
MOTOR_STEP_PORT->BSRR = (uint32_t)MOTOR_STEP_PIN << 16;  /* BSRR[31:16] = reset */
```

**涉及文件:** `firmware/Drivers/BSP/motor/a4988.c` L225

---

### 问题 10: `__HAL_TIM_GENERATE_EVENT` 隐式声明

**严重性:** ❌ ERROR (编译失败, 1 error — ARMCC 将隐式函数声明视为错误)

**错误信息:**
```
../Drivers/BSP/motor/a4988.c(187): warning: #223-D: function "__HAL_TIM_GENERATE_EVENT" declared implicitly
```

**原因:** `__HAL_TIM_GENERATE_EVENT` 在 STM32F4 HAL 中是通过 TIM 头文件定义的宏，但部分 HAL 版本未导出。直接操作寄存器更可靠。

**修复:**
```c
// 修复前
__HAL_TIM_GENERATE_EVENT(&htim2, TIM_EVENTSOURCE_UPDATE);

// 修复后
htim2.Instance->EGR = TIM_EGR_UG;
```

**涉及文件:** `firmware/Drivers/BSP/motor/a4988.c` L187

---

### 问题 11: `base_w` 变量未使用

**严重性:** ⚠️ WARNING

**错误信息:**
```
../Drivers/BSP/lcd/st7735.c(510): warning: #550-D: variable "base_w" was set but never used
```

**修复:** 移除无用变量及 `switch` 中对应的赋值：
```c
// 修复前
uint8_t scale;
uint8_t base_w = 5;
switch (font) {
    case FONT_6x8:   scale = 1; break;
    case FONT_8x16:  scale = 1; base_w = 8; break;
    ...
}

// 修复后
uint8_t scale;
switch (font) {
    case FONT_6x8:   scale = 1; break;
    case FONT_8x16:  scale = 1; break;
    ...
}
```

**涉及文件:** `firmware/Drivers/BSP/lcd/st7735.c` L510

---

### 问题 12: `w25q64_read_status2` 未引用

**严重性:** ⚠️ WARNING

**错误信息:**
```
../Drivers/BSP/flash/w25q64.c(166): warning: #177-D: function "w25q64_read_status2" was declared but never referenced
```

**修复:** 将未使用的静态函数注释掉（保留代码以便未来启用）：
```c
// 修复前
static uint8_t w25q64_read_status2(void) { ... }

// 修复后
/*
static uint8_t w25q64_read_status2(void) { ... }
*/
```

**涉及文件:** `firmware/Drivers/BSP/flash/w25q64.c` L166

---

### 问题 13: `finish_tick` 变量未使用

**严重性:** ⚠️ WARNING

**错误信息:**
```
../App/modes/timer_mode.c(39): warning: #550-D: variable "finish_tick" was set but never used
```

**修复:** 移除变量声明及两处赋值：
```c
// 修复前
static uint32_t start_tick;
static uint32_t finish_tick;      // ← 删除

// 修复后
static uint32_t start_tick;
```

**涉及文件:** `firmware/App/modes/timer_mode.c` L39, L59, L87

---

## Keil 工程配置修改

除代码修复外，还对 [ov-watch.uvprojx](../../firmware/MDK-ARM/ov-watch.uvprojx) 做了以下配置修改：

### IncludePath 更新

C 编译器（Cads）和汇编器（Aads）各新增 12 个路径：
```
../Drivers/BSP/lcd
../Drivers/BSP/flash
../Drivers/BSP/motor
../Drivers/BSP/button
../Drivers/BSP/sensor
../Drivers/BSP/rtc
../Middleware/gui
../Middleware/pointer
../Middleware/fs
../App
../App/modes
../App/tasks
```

### 源文件分组

新增 3 个工程分组，包含全部 15 个 `.c` 文件：

| Group | 文件 |
|-------|------|
| `Drivers/BSP/` | st7735.c, w25q64.c, a4988.c, button.c, temp_adc.c, temp_sensor.c, rtc_drv.c |
| `Middleware/User/` | gui.c, pointer_engine.c, fs_mgr.c |
| `Application/User/App/` | mode_manager.c, clock_mode.c, temp_mode.c, timer_mode.c, settings_mode.c |

---

## 修复统计 (全部两轮)

### 第一轮（静态审查）

| 文件 | 修改次数 | 类型 |
|------|----------|------|
| `App/modes/temp_mode.c` | 3 | `#include` + 2 处 `gui_dirty_mark()` 参数 |
| `App/modes/settings_mode.c` | 1 | 22 处 `gui_dirty_mark()` 全局替换 |
| `App/tasks/tasks.h` | 1 | 4 个符号名修正 |
| `Core/Inc/pin_config.h` | 1 | `ADC_CHANNEL_10` → `ADC_CHANNEL_TEMPSENSOR` |

### 第二轮（Keil ARMCC V5 编译）

| 文件 | 修改次数 | 类型 |
|------|----------|------|
| `App/modes/clock_mode.h` | 1 | `#include "button.h"` |
| `App/modes/temp_mode.h` | 1 | `#include "button.h"` |
| `App/modes/timer_mode.h` | 1 | `#include "button.h"` |
| `App/modes/settings_mode.h` | 1 | `#include "button.h"` |
| `Middleware/gui/gui.c` | 1 | 64 字节 `0bXX` → `0xXX` |
| `Drivers/BSP/sensor/temp_adc.c` | 1 | `ADC_REGULAR_RANK_1` → `1` |
| `Drivers/BSP/motor/a4988.c` | 3 | BRR→BSRR, __HAL_TIM_GENERATE_EVENT→EGR |
| `Drivers/BSP/lcd/st7735.c` | 1 | 移除 unused `base_w` |
| `Drivers/BSP/flash/w25q64.c` | 1 | 注释 unused 函数 |
| `App/modes/timer_mode.c` | 3 | 移除 unused `finish_tick` |
| `MDK-ARM/ov-watch.uvprojx` | 3 | IncludePath ×2 + 源文件分组 |

### 总计

| 指标 | 数值 |
|------|------|
| 修复文件数 (代码) | 14 |
| 修复文件数 (工程配置) | 1 |
| 修复问题类别 | 13 |
| 消除 ERROR | 45 |
| 消除 WARNING | 5 |
| 残留 WARNING | 4 (多字节字符编码，不影响编译) |

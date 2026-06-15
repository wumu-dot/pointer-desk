# OV-Watch 运行时 Bug 修复记录

> 日期：2026-06-15
> 状态：全部已修复
> 阶段：首次上电验证 + 第二轮性能优化

---

## 问题汇总

| # | 严重性 | 现象 | 涉及文件 | 类型 |
|---|--------|------|----------|------|
| 14 | ❌ ERROR | 链接失败 `multiply defined` | `Core/Src/main.c` | 符号重复定义 |
| 15 | ❌ ERROR | 编译 `out of store` | `ov-watch.uvprojx` | 编译器内存溢出 |
| 16 | ⚠️ WARNING | `invalid multibyte character` | `Middleware/fs/fs_mgr.c` L157 | 非ASCII字符 |
| 17 | ❌ BUG | TFT 数字完全看不清 | `Drivers/BSP/lcd/st7735.c` L500-567 | 字模读取行列颠倒 |
| 18 | ⚠️ BUG | 串口 LOG 疯狂刷屏 | `Middleware/pointer/pointer_engine.c` L122 | 无条件日志 |
| 19 | ⚠️ BUG | 脏矩形频繁溢出全屏折叠 | `App/modes/clock_mode.c` L141-235 | 冗余脏矩形标记 |
| 20 | ❌ BUG | 物理指针永远停在0°不转 | `Core/Src/freertos.c` L147-153 | 电机反馈回路覆盖目标 |
| 21 | ⚠️ BUG | temp/timer/settings 每20ms全屏重绘 | `App/modes/temp_mode.c` `timer_mode.c` `settings_mode.c` | 无渲染去重 |
| 22 | ⚠️ PERF | SPI逐像素发送 → 渲染卡顿约300ms/帧 | `Drivers/BSP/lcd/st7735.c` | SPI调用过多 |
| 23 | ⚠️ PERF | 每个字符独立设置LCD窗口 | `Drivers/BSP/lcd/st7735.c` L585-595 | 冗余CASET/RASET |
| 24 | 💀 DEAD | `state.scale` 字段8次写入0次读取 | `Middleware/pointer/pointer_engine.c` | 死代码 |
| 25 | 🔧 REFACTOR | draw_char裁剪与clip_rect重复 | `Drivers/BSP/lcd/st7735.c` L519-521 | 代码重复 |
| 26 | 🔧 REFACTOR | 0xFF哨兵值意图不清晰 | `App/modes/clock_mode.c` L25-35 | 可维护性 |

---

## 问题 14: `StartTask*` 符号重复定义

**严重性:** ❌ ERROR (链接失败, 4 errors)

**错误信息:**
```
Error: L6200E: Symbol StartTaskBG multiply defined (by freertos.o and main.o).
Error: L6200E: Symbol StartTaskButton multiply defined (by freertos.o and main.o).
Error: L6200E: Symbol StartTaskDisplay multiply defined (by freertos.o and main.o).
Error: L6200E: Symbol StartTaskMotor multiply defined (by freertos.o and main.o).
```

**原因:** [main.c](../../firmware/Core/Src/main.c) 中 CubeMX 生成了 4 个任务函数的空壳定义（每个都是 `for(;;) osDelay(1)`），而 [freertos.c](../../firmware/Core/Src/freertos.c) 中包含了真正的任务实现。两个编译单元同时导出相同符号，链接器无法抉择。

**涉及位置:**
- [main.c:875-938](../../firmware/Core/Src/main.c#L875) — CubeMX 空壳定义
- [freertos.c:74-181](../../firmware/Core/Src/freertos.c#L74) — 真实实现

**修复:** 删除 `main.c` 中 L867-L938 的全部 4 个空壳函数体，保留 L137-L140 的前向声明（`main()` 中 `osThreadNew` 调用需要这些声明）。

```c
// 保留 (main.c 第137-140行)
void StartTaskButton(void *argument);
void StartTaskBG(void *argument);
void StartTaskDisplay(void *argument);
void StartTaskMotor(void *argument);

// 删除 (main.c 第867-938行)
// → 4个 { for(;;) { osDelay(1); } } 空壳函数体
```

**涉及文件:** `firmware/Core/Src/main.c`

---

## 问题 15: 编译器 `out of store` 内存溢出

**严重性:** ❌ ERROR (编译失败, 部分文件)

**错误信息:**
```
Error: C4048U: out of store while compiling with -g. Allocation size was 1048576, system size is 4892860
Error: C4365E: Subtool invocation error: Error executing C:\keil5\ARM\ARMCC\Bin\armasm.exe.
Error: CreateProcess failed, Command: '"C:\keil5\ARM\ARMCC\Bin\ArmCC" --via "ov-watch\temp_sensor.__i"'
```

**原因:** ARM Compiler V5 是 32 位应用，编译器堆内存上限约 4.8 MB。同时开启 `Optim:4`（跨模块优化）和 `DebugInformation:1`（DWARF 调试符号）时，大文件的符号表直接超过编译器堆限制。连锁反应：编译崩溃 → 系统资源耗尽 → 后续文件 `CreateProcess failed`。

**修复:** 在 [ov-watch.uvprojx](../../firmware/MDK-ARM/ov-watch.uvprojx) 中关闭全局调试信息：

```xml
<!-- 修复前 -->
<DebugInformation>1</DebugInformation>
<!-- 修复后 -->
<DebugInformation>0</DebugInformation>
```

**涉及文件:** `firmware/MDK-ARM/ov-watch.uvprojx` L56

**后续方案:** 如需调试能力，建议创建第二个 Keil Target 设 `Optim:0` + `DebugInformation:1`，或切换到 ARM Compiler V6。

---

## 问题 16: 字符串字面量中的 em dash 多字节字符

**严重性:** ⚠️ WARNING (1 warning)

**错误信息:**
```
../Middleware/fs/fs_mgr.c(157): warning: #870-D: invalid multibyte character sequence
```

**原因:** [fs_mgr.c](../../firmware/Middleware/fs/fs_mgr.c#L157) 的 LOG 字符串中使用了 em dash `—` (U+2014)，ARM Compiler V5 不支持源代码中的 UTF-8 多字节序列。

**修复:**
```c
// 修复前
LOG("Bad magic 0x%08lX, expected 0x%08lX — formatting", ...);
// 修复后
LOG("Bad magic 0x%08lX, expected 0x%08lX -- formatting", ...);
```

**涉及文件:** `firmware/Middleware/fs/fs_mgr.c` L157

**备注:** 项目中大量注释使用了 em dash 和中文，注释中的多字节字符不影响编译，只有字符串字面量中的才会触发警告。

---

## 问题 17: 字模数据行列读取颠倒 → TFT 数字完全看不清

**严重性:** ❌ BUG (运行时显示错误, 无编译错误)

**现象:** TFT 显示屏上的数字和文字完全不可辨认，呈现为随机噪点。

**原因:** [st7735.c](../../firmware/Drivers/BSP/lcd/st7735.c#L373) 的字库注释和实际存储格式是：

> 每个字符 **5 列，每列一个字节 (bit0=顶行, bit6=底行)**

即 `font_5x7[字符][列]`，列内字节从 bit0 (顶行) 到 bit6 (底行) 编码 7 行。

但渲染代码错误地当作**行优先**来读：`font_5x7[idx][base_row]`（还越界到第6个元素），然后用 `>> (4-base_col)` 提取水平方向比特。

**正确逻辑:**
```c
// 修复前 (行列颠倒)
line = font_5x7[idx][base_row];           // ← 5列数组按行索引
pixel_on = (line >> (4 - base_col)) & 0x01;

// 修复后 (列优先)
// font_5x7[idx][base_col] = 该列的7行垂直位图
pixel_on = (font_5x7[idx][base_col] >> base_row) & 0x01;
```

**视觉示例 — 字符 '0':**

字库数据 `{0x3E, 0x51, 0x49, 0x45, 0x3E}`:
```
列0 (0x3E = 0b0111110):  行6 行5 行4 行3 行2 行1 行0 → ○●●●●●○
列1 (0x51 = 0b1010001):  1   0   1   0   0   0   1 → ●○●○○○●
列2 (0x49 = 0b1001001):  1   0   0   1   0   0   1 → ●○○●○○●
列3 (0x45 = 0b1000101):  1   0   0   0   1   0   1 → ●○○○●○●
列4 (0x3E = 0b0111110):  0   1   1   1   1   1   0 → ○●●●●●○
```

按原来错误逻辑读出来就是完全随机的噪点。

**涉及文件:** `firmware/Drivers/BSP/lcd/st7735.c` L500-L567

**字模数据来源:** 标准 5×7 ASCII 字库，数据本身正确。问题出在渲染代码。

---

## 问题 18: `pointer_set_target()` 无条件 LOG 刷屏

**严重性:** ⚠️ BUG (功能正常, 但串口输出被淹没)

**现象:** 串口每隔约 50ms 输出 `[LOG] Pointer target set: 0.0 deg (mode 0)`，每秒约 20-40 条，淹没其他调试信息。

**原因:** [pointer_engine.c](../../firmware/Middleware/pointer/pointer_engine.c#L116) 中 `pointer_set_target()` 每次被调用都无条件打印日志。调用链：
```
BG任务(每20ms) → mode_manager_update(每100ms) → clock_mode_update()
  → pointer_set_clock() → pointer_set_target()  // ← 刷屏点
```

**修复:** 仅在目标角度（容差 0.05°）或移动模式实际变化时才输出 LOG：

```c
// 修复后
float new_target = normalize_angle(angle);
bool angle_changed = (fabsf(angle_diff(state.target_angle, new_target)) > 0.05f);
bool mode_changed  = (state.move_mode != mode) || !state.is_moving;
state.target_angle = new_target;
state.move_mode    = mode;
state.is_moving    = true;
if (angle_changed || mode_changed) { LOG(...); }  // ← 仅变化时打印
```

**涉及文件:** `firmware/Middleware/pointer/pointer_engine.c` L116-L123

---

## 问题 19: `clock_mode_render()` 冗余脏矩形标记

**严重性:** ⚠️ BUG (功能正常, 但易触发脏矩形溢出 + 全屏折叠 LOG)

**原因:** `gui_draw_icon()` 和 `gui_draw_text_centered()` 内部已经调用了 `gui_dirty_mark()`，但 `clock_mode_render()` 又追加了显式标记。导致每次渲染产生的脏矩形数接近 `MAX_DIRTY_RECTS=8` 上限，频繁触发全屏折叠。

**修复:** 移除 3 处冗余的 `gui_dirty_mark()`，仅保留 `st7735_fill_rect()` 之后的显式标记（fill_rect 是底层调用，不经 gui 层，不会自动标记脏区）。

```c
// 修复后
gui_draw_icon(...);                // 内部已标记脏区 ✓
st7735_fill_rect(...);            // 底层调用，需显式标记
gui_dirty_mark(...);               // ← 保留
gui_draw_text_centered(...);       // 内部已标记脏区 ✓
```

**涉及文件:** `firmware/App/modes/clock_mode.c` L157-L207

---

## 问题 20: 电机反馈回路覆盖目标角度 → 物理指针永远不转

**严重性:** ❌ BUG (功能完全失效)

**现象:** 烧录后指针不随时间转动，始终停在初始位置 0°。

**原因:** [freertos.c](../../firmware/Core/Src/freertos.c#L147-L153) 的 BG 任务每 20ms 把 `pointer_get_current_angle()`（0°）作为"目标"发送到电机队列。电机任务读出后用这个当前位置覆盖了 `clock_mode_update()` 设置的真实目标。

```
T=0ms  clock_mode_update() → pointer_set_target(315°)     ← 真实目标
T=0ms  BG任务发送 motor{target=0°} → 队列积压
T=50ms 电机读取 motor{target=0°} → pointer_set_target(0°)  ← 覆盖! 电机不走
```

**修复:** 两处改动：

1. **删除 BG 任务 L147-L153** — 移除整个"电机角度 → 电机任务"反馈块。
2. **重写电机任务** — 改为无条件每 50ms 调用 `pointer_engine_update()`，队列改为非阻塞读取仅用于接收新目标。

```c
// 修复后 StartTaskMotor
for (;;) {
    motor_msg_t target;
    if (osMessageQueueGet(..., 0) == osOK) {  // 非阻塞
        pointer_set_target(target.target_angle, ...);
    }
    pointer_engine_update();  // 无条件驱动平滑插值
    // ... 电机控制
    osDelay(pdMS_TO_TICKS(50));
}
```

**涉及文件:** `firmware/Core/Src/freertos.c` L147-L213

---

## 问题 21: temp_mode / timer_mode / settings_mode 无渲染去重

**严重性:** ⚠️ BUG (每 20ms 全屏重绘, 屏幕持续闪烁)

**原因:** 只有 `clock_mode_render()` 有 `display_changed()` 缓存检测。其余三个模式每次被 BG 任务调用时无条件重绘。

**修复:**

- **temp_mode_render()** — 缓存温度值（容差 0.05°C）+ 单位 + 传感器标签，不变则跳过。
- **timer_mode_render()** — 缓存 `remaining_sec`、`total_sec`、`state`，三者都不变则跳过。
- **settings_mode_render()** — 使用脏标志 `needs_render`。`enter()` 和按键处理函数置 `true`，`render()` 末尾置 `false`。纯静态页面，仅按键时刷新。

**涉及文件:**
- `firmware/App/modes/temp_mode.c`
- `firmware/App/modes/timer_mode.c`
- `firmware/App/modes/settings_mode.c`

---

## 问题 22: SPI 逐像素逐字节发送 → 渲染卡顿约 300ms/帧

**严重性:** ⚠️ PERF (渲染速度慢, 屏幕逐行刷出)

**原因:** `st7735_draw_char()` 对每个像素调用 2 次 `HAL_SPI_Transmit(1 字节)`，`lcd_write_color_bulk()` 同理。一次时钟全屏刷新 ≈ 9000-10000 次 SPI 单字节调用。

**修复:**

1. **`lcd_write_color_bulk()`** — 128 字节静态缓冲区预填充 hi/lo 数据，每 64 像素一次 SPI 调用。1000 像素矩形从 2000 次降为 ~16 次。
2. **`st7735_draw_char()`** — 提取 `draw_char_raw()`，每行先组包到 `row_buf[32]`，整行一次 SPI 发送。12×24 字符从 576 次降为 24 次。

**涉及文件:** `firmware/Drivers/BSP/lcd/st7735.c`

---

## 问题 23: 每个字符独立设置 LCD 窗口

**严重性:** ⚠️ PERF (冗余 CASET/RASET 命令)

**原因:** `st7735_draw_text()` 逐字符调用 `st7735_draw_char()` → 每字符一次 `st7735_set_window()`（11 次 SPI 命令）。8 字符文本 = 88 次冗余 SPI 命令。

**修复:** 预先计算整行总宽度，一次性设窗口，跨字符保持 CS 低电平发送像素。同时修复裁剪后未限制字符数的问题。

```c
// 修复后
st7735_set_window(x, y, total_w, total_h);  // 一次性
cs_low(); dc_high();
while (*str) { draw_char_raw(idx, ...); str++; }
cs_high();
```

**涉及文件:** `firmware/Drivers/BSP/lcd/st7735.c` L585-L618

---

## 问题 24: `state.scale` 字段只写不读 — 死代码

**严重性:** 💀 DEAD (8 处写入, 0 处读取)

**原因:** `pointer_state_t.scale` 在 8 个函数中赋值（init、clock、temperature ×2、timer ×2、page），但整个项目无任何代码读取此字段。`pointer_get_state()` 函数也无人调用。

**修复:** 移除全部 8 处 `state.scale = ...` 语句。保留枚举定义以备将来使用。

**涉及文件:** `firmware/Middleware/pointer/pointer_engine.c`

---

## 问题 25: `st7735_draw_char` 裁剪逻辑与 `clip_rect` 重复

**严重性:** 🔧 REFACTOR (功能正确, 但维护风险高)

**原因:** L519-L521 手动裁剪代码与 `clip_rect()` 完全重复。未来修改屏幕分辨率可能两处不一致。

**修复:** 删除手动裁剪，统一使用 `clip_rect()`。

```c
uint16_t x16 = x, y16 = y, w16 = cw, h16 = ch;
if (!clip_rect(&x16, &y16, &w16, &h16)) return;
cw = (uint8_t)w16; ch = (uint8_t)h16;
```

**涉及文件:** `firmware/Drivers/BSP/lcd/st7735.c` L519-L521

---

## 问题 26: `clock_mode.c` 0xFF 哨兵值不够健壮

**严重性:** 🔧 REFACTOR (实践中安全, 但意图不清晰)

**原因:** 缓存初始化 `{ 0xFF, 0xFF, 0xFF, ... }`，依赖 0xFF 不是有效 RTC BIN 值来触发首次渲染。需要隐含前提知识才能理解。

**修复:** 添加 `static bool need_full_refresh = true` 标志，替换全部 0xFF 哨兵。`display_changed()` 首行检查此标志，`update_cache()` 末尾清零，`init()`/`enter()`/`trigger_full_refresh()` 中置位。

**涉及文件:** `firmware/App/modes/clock_mode.c`

---

## 自审发现 (第三轮: 代码自审)

> 日期：2026-06-15
> 第二轮优化后，系统性地自审了所有修改，发现并修复了 2 个问题。

### 问题 27: `st7735_draw_text` 多字符数据错位 ❌

**严重性:** ❌ BUG (所有多字符文本渲染错误)

**现象:** 所有通过 `st7735_draw_text` 绘制的文本（时间、日期、菜单项等）会被水平展开成碎片。

**原因:** ST7735 在 RAMWR 模式下按**行优先**写入：先写完第一行的所有列，然后自动换行写第二行的所有列…。

`draw_char_raw('A')` 对窗口 36×24 发送 12 列 × 24 行 = 288 像素时：
- ST7735 不会在每 12 个像素后换行（因为列计数器未到窗口末尾 36）
- 导致 'A' 的 24 行字模数据被水平展开成 8 个显示行的碎片
- 然后 'B' 继续从下一列开始写，完全错位

**修复:** 回退 `st7735_draw_text` 为逐字符调用 `st7735_draw_char()`。每个字符独立设 12×24 窗口，ST7735 能在每 12 像素后正确换行。`draw_char_raw` 的逐行批量 SPI 优化（24 次 SPI/字符 vs 576 次）仍保留。

**涉及文件:** `firmware/Drivers/BSP/lcd/st7735.c` L588-L622
**修复提交:** #23 部分回退

---

### 问题 28: settings DATETIME 时钟显示冻结 ⚠️

**严重性:** ⚠️ BUG (显示不正确, 但设置模式使用频率低)

**现象:** 进入 settings → Date & Time 后，时间显示凝固在进入时刻，不会更新。

**原因:** `needs_render` 标志只在按键事件和 `enter()` 时置 true。DATETIME 子状态只处理 `BTN_LEFT`（退出），无其他按键事件。导致时间显示永不刷新。

**修复:** 在 `settings_mode_update()` 中添加 DATETIME 专用刷新逻辑：每 1000ms 自动置 `needs_render = true`。其他子状态不受影响（它们没有周期性内容需要更新）。

**涉及文件:** `firmware/App/modes/settings_mode.c` L114-L128

---

### 审查通过项 (无需修改)

| 检查项 | 结论 |
|--------|------|
| `lcd_write_color_bulk` static 缓冲区 | 安全，128 字节 BSS 可接受 |
| 电机任务 `EvtMotorHandle` 每 50ms 设置 | 无害，无人等待此事件标志 |
| `clock_mode.c` 无重复 `update_cache` 定义 | 已删除旧重复 |
| `timer_mode` 按钮触发 cache miss | 所有状态变化路径均正确检测 |
| `temp_mode` 单位切换 cache miss | `fahrenheit` 变化导致 cache miss |
| `clip_rect` 不修改 `*x`/`*y` | `set_window(x, y, ...)` 使用原始坐标安全 |
| Flash 擦写频率 | 无后台写入，无磨损风险 |

---

## 第四轮: 上电验证发现

> 日期：2026-06-15
> 烧录后用户反馈: 时间不对 + 屏幕高频闪烁

### 问题 29: RTC 时间始终错误 ❌

**严重性:** ❌ BUG (时间显示不正确)

**现象:** 显示屏上的时间始终不对，偏移量不可预测。

**原因:** 两个独立问题叠加：

1. **格式冲突**: `MX_RTC_Init()` 用 `RTC_FORMAT_BCD` 写入时间值（如 Month = `RTC_MONTH_JANUARY` = 0x01），但 `rtc_drv_get_datetime()` 用 `RTC_FORMAT_BIN` 读取。HAL 内部的 BCD↔BIN 转换路径不一致，导致读出的值错位。
2. **写入被跳过**: `sTime.StoreOperation = RTC_STOREOPERATION_RESET` 告诉 HAL **不存储时间**。RTC 寄存器保留上电后的随机值。
3. **每次开机重写**: 原 CubeMX 代码无条件每 次启动都设置时间到默认值。

**修复:**

```c
// MX_RTC_Init() L424-L472

// 1) 备份寄存器检查: 仅首次上电设默认时间
if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) != 0x55AA) {
    need_set_time = true;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0x55AA);
}

// 2) 改用 RTC_FORMAT_BIN (与 rtc_drv_get_datetime 一致)
HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

// 3) 改用 RTC_STOREOPERATION_SET (真正写入)
sTime.StoreOperation = RTC_STOREOPERATION_SET;
```

**涉及文件:** `firmware/Core/Src/main.c` L421-L472

---

### 问题 30: TFT 显示屏高频闪烁 ⚠️

**严重性:** ⚠️ BUG (功能正常但视觉体验差)

**现象:** 屏幕每秒刷新一次时，可见明显的逐行刷出和闪烁。

**原因:** STM32F4 APB1 时钟 42 MHz，SPI1 预分频器 `SPI_BAUDRATEPRESCALER_16`：
```
SPI 时钟 = 42 MHz / 16 = 2.625 MHz
一次全屏渲染 ≈ 4260 像素 × 2 字节 = 8520 字节
纯传输时间 = 8520 × 8 / 2.625M ≈ 26 ms (不含HAL开销)
实际耗时 ≈ 50-100 ms (含CS/DC切换和HAL_TRANSMIT开销)
```
渲染时间占 1 秒周期的 5-10%，肉眼可见行扫描。

**修复:** 预分频器改为 `SPI_BAUDRATEPRESCALER_4`：
```
SPI 时钟 = 42 MHz / 4 = 10.5 MHz
纯传输时间 ≈ 6.5 ms (4× faster)
实际耗时 ≈ 15-25 ms (可接受)
```
ST7735 最大 SPI 时钟 ≈ 15 MHz，10.5 MHz 在安全范围内。

**涉及文件:** `firmware/Core/Src/main.c` L509

---

## 修复统计

### 第一轮（编译 + 首次上电）

| 文件 | 修改 | 对应问题 |
|------|------|----------|
| `Core/Src/main.c` | 删除 4 个重复函数定义 | #14 |
| `MDK-ARM/ov-watch.uvprojx` | 关闭 DebugInformation | #15 |
| `Middleware/fs/fs_mgr.c` | em dash → `--` | #16 |
| `Drivers/BSP/lcd/st7735.c` | 字模读取行列修正 | #17 |
| `Middleware/pointer/pointer_engine.c` | LOG 去重 | #18 |
| `App/modes/clock_mode.c` | 移除冗余脏矩形标记 | #19 |

### 第二轮（性能优化 + 代码清理）

| 文件 | 修改 | 对应问题 |
|------|------|----------|
| `Core/Src/freertos.c` | 删除电机反馈回路 + 电机任务重构 | #20 |
| `App/modes/temp_mode.c` | 渲染去重缓存 | #21 |
| `App/modes/timer_mode.c` | 渲染去重缓存 | #21 |
| `App/modes/settings_mode.c` | needs_render 脏标志 + DATETIME 刷新 | #21 #28 |
| `Drivers/BSP/lcd/st7735.c` | SPI 批量发送 + clip_rect 统一 + draw_text 回退 | #22 #25 #27 |
| `Middleware/pointer/pointer_engine.c` | 移除 scale 死代码 | #24 |
| `App/modes/clock_mode.c` | need_full_refresh 标志替代 0xFF | #26 |

### 第四轮（上电验证修复）

| 文件 | 修改 | 对应问题 |
|------|------|----------|
| `Core/Src/main.c` | RTC BCD→BIN + STOREOPERATION_SET + 条件写入 | #29 |
| `Core/Src/main.c` | SPI 预分频 16→4 (2.625→10.5 MHz) | #30 |

### 总计

| 指标 | 数值 |
|------|------|
| 修复文件数 | 10 |
| 修复问题数 | 18 (#14-#31) |
| Flash 擦写风险 | 无（已验证） |

---

## 第五轮: 上电验证

### 问题 31: W25Q64 Flash 操作全部超时 ❌

**严重性:** ❌ BUG (Flash 初始化缺失 → fs 格式化全部失败)

**错误信息:**
```
[ERR] Flash operation timed out after 400 ms (w25q64.c:188)
```  
255 次，每秒一次，持续约 3 分钟。

**原因:** `w25q64_init()` 在整个项目中从未被调用。该函数负责：
1. 初始化 CS 引脚（`GPIO_MODE_OUTPUT_PP`）
2. 唤醒芯片（退出 Power Down 模式）
3. 发送 JEDEC ID 查询确认通信正常

缺失 `w25q64_init()` 导致 CS 引脚 **从未被配置**，始终浮空。`w25q64_read()` 读到全 1 (`0xFFFFFFFF`) 会误触发 `format_internal()`，然后 255 次 `w25q64_erase_sector()` 因为 CS 未受控而全部超时。

**修复:** 在 `main.c` 用户初始化段，BSP 驱动初始化区，添加 `w25q64_init()` 调用（位于 `MX_SPI2_Init()` 之后、调度器启动之前）：

```c
  /* ---- BSP 驱动初始化 ---- */
  st7735_init();
  button_init();
  a4988_init();
  temp_sensor_init();
  w25q64_init();  // ← 新增
```

**涉及文件:** `firmware/Core/Src/main.c` L197

---

### 问题 32: 基准时间无法修改 + rtc_drv_set_datetime 从未调用 ❌

**严重性:** ❌ BUG (RTC 时间永远 2000-01-01 00:00:00)

**原因:** `rtc_drv_set_datetime()` API 已实现但整个项目中调用次数为 0。Date & Time 设置界面只有显示无编辑。用户无法改变基准时间。

**修复:** 将 `ST_DATETIME` 从只读显示改为可编辑编辑器：
- 进入时从 RTC 读取当前值 → 5 个编辑字段(Year/Month/Day/Hour/Minute)
- UP/DOWN 调整当前字段值, LEFT/RIGHT 导航字段
- CENTER 保存到 RTC, 回到主菜单
- 闰年处理: `days_in_month()` 辅助函数

**涉及文件:** `firmware/App/modes/settings_mode.c` L29-L50, L227-L550

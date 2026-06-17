# OV-Watch Bug 清单

> 日期：2026-06-17  
> 编译：0 Error, 4 Warning（已知无害）  
> 硬件：鹿小班 STM32F407ZGT6 + 1.8寸 ST7735S

---

## 🔴 P0 — 阻塞性

### #1 TFT 显示闪烁（撕裂/Tearing）

**现象**：时钟每秒刷新时，屏幕可见闪烁/撕裂线。

**根因**（对照ST7735S数据手册第9.12节）：
- ST7735S支持TE（Tearing Effect）硬件同步信号，芯片第14脚
- 1.8寸公版模块**未引出TE引脚**到排针，无法用硬件同步
- 无TE信号时，SPI写入GRAM与面板扫描异步 → 必然出现撕裂
- 数据手册："Data write to Frame Memory should be written during the vertical sync pulse"

**已尝试的软件修复**：
- SPI时钟 21MHz → 10.5MHz（满足≤15MHz规格）
- 帧缓冲 + 选择性渲染（每秒只flush ~2.3KB时间区域）
- 脏矩形合并后单次flush
- 结果：仍有闪烁，软件层面无法根治

**建议修复**：
1. **飞线TE引脚**（推荐）：ST7735S第14脚飞线到STM32空闲GPIO（如PB6/PE2），`flush_rect()`前轮询TE
2. **提高刷新率**：改为20FPS持续刷新，把撕裂线埋在高帧率中（功耗增加）
3. **接受现状**：公版模块的硬件限制，不影响功能正确性

---

## 🟡 P1 — 功能受限

### #2 RTC 断电后时间丢失

**现象**：断电后RTC时间归零，需重新编译/烧录注入时间。

**根因**：VBAT引脚未接备用电池。

**影响**：每次断电后时间混乱。

**修复**：接CR1220纽扣电池到VBAT引脚。

---

### #3 温度读数不准确（±5°C）

**现象**：温度显示误差大。

**根因**：SHT30传感器（I2C2）未接，回退到STM32内部ADC温度通道，精度仅±5°C。

**修复**：接SHT30到I2C2（PB10=SCL, PB11=SDA, 地址0x44），`temp_sensor.c`已预留接口。

---

### #4 无法保存设置

**现象**：亮度、12/24h格式等设置断电后丢失。

**根因**：W25Q64 Flash（SPI2）未接。检测不到时自动跳过文件系统初始化。

**影响**：
- 亮度设置不持久
- 倒计时默认值不记忆
- 温度单位（℃/℉）不记忆
- 12/24h格式已通过RTC备份寄存器保存（不受影响）

**修复**：接W25Q64到SPI2（PB12=CS, PB13=SCK, PB14=MISO, PB15=MOSI）。

**涉及文件**：`w25q64.c`, `fs_mgr.c`

---

### #5 物理指针未验证

**现象**：指针引擎代码无法测试。

**根因**：步进电机 + A4988驱动模块未连接。

**涉及文件**：`pointer_engine.c`, `a4988.c`

**修复**：接电机 + A4988到PA0(STEP)/PA1(DIR)/PA2(EN)。

---

## 🟢 P2 — 体验优化

### #6 settings 模式下时间不自动更新

**现象**：settings→Date & Time 页面，时间显示冻结在进入时刻。

**根因**：settings模式下 `needs_render` 仅在按键事件时置 true，DATETIME 子状态无按键事件触发刷新。

**状态**：已在 `runtime-bugs.md` #28 记录修复（每1000ms自动置 `needs_render=true`），需确认当前代码是否生效。

**涉及文件**：`settings_mode.c` L79-82

---

### #7 timer 模式无法调整时长

**现象**：单键模式下只能长按开始/暂停，无法增减倒计时分钟数。

**根因**：单键适配后移除了UP/DOWN按键逻辑。单键无多余操作可分配。

**建议方案**：
- 双击 → 进入时长调整模式（闪烁显示分钟数）
- 长按 → 调整模式内增加1分钟，循环
- 三击/超时 → 退出调整模式

---

## ⚪ P3 — 代码质量

### #8 4个编译Warning

| 文件 | 行 | Warning | 原因 |
|------|-----|---------|------|
| `w25q64.c` | 237 | `#870-D: invalid multibyte character` | LOG字符串含em dash `—` |
| `mode_manager.c` | 69 | `#188-D: enumerated type mixed with another type` | `mode_id_t i = 0` |
| `mode_manager.c` | 81 | `#870-D: invalid multibyte character` | LOG字符串含em dash `—` |
| `mode_manager.c` | 141 | `#188-D: enumerated type mixed with another type` | `mode_id_t next = (current_mode + 1)` |

**影响**：无，ARMCC V5 对枚举和UTF-8较宽松。修复仅需替换em-dash为`--`，枚举加`(mode_id_t)`强制转换。

---

## 📊 统计

| 优先级 | 数量 | 说明 |
|--------|------|------|
| P0 阻塞 | 1 | TFT闪烁（硬件限制） |
| P1 功能 | 4 | 外设未接（RTC/温度/Flash/电机） |
| P2 体验 | 2 | 软件可修复 |
| P3 质量 | 1 | 编译警告 |
| **总计** | **8** | |

---

## ✅ 已修复（本轮）

| # | 问题 | 状态 |
|----|------|------|
| SPI时钟21MHz超规格 | ✏️ 84/8=10.5MHz | ✅ |
| SPI1时钟源算错（APB2非APB1） | ✏️ 注释修正 | ✅ |
| init后TFT GRAM未同步 | ✏️ memset后加flush | ✅ |
| 多按键→单键适配 | ✏️ mode_manager各模式 | ✅ |
| 全屏40KB DMA刷新 | ✏️ 改为脏矩形局部刷新 | ✅ |
| 14-32号bug（来自runtime-bugs.md） | ✏️ 18个问题 | ✅ |
| fs_config_save Flash 空间泄漏 | ✏️ 每次保存强制追加到队尾，g_next_data_offset 只进不退，Flash 空间被无意义消耗 | ✅ 2026-06-17 |

> 编译修复历史见 [compile-fixes.md](docs/superpowers/compile-fixes.md)（13类问题，45个ERROR）  
> 运行时修复历史见 [runtime-bugs.md](docs/superpowers/runtime-bugs.md)（#14-#31，18个问题）

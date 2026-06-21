# Bug 记录索引

> 一行一条 Bug，详情点进文件看。按修复日期倒序，修完即更新。

## 已知未修复

| Bug ID | 标题 | 等级 | 发现 | 备注 |
|--------|------|------|------|------|
| BUG-20260610-001 | [TFT 显示闪烁（TE 引脚未引出）](BUG-20260610-001-tft-tearing.md) | P0 | 06-10 | 硬件限制，软件无法根治 |
| BUG-20260610-002 | [RTC 断电后时间丢失](BUG-20260610-002-rtc-power-loss.md) | P3 | 06-10 | VBAT 未接电池；ESP32 NTP 每60s校准已缓解 |
| BUG-20260610-004 | [步进电机 + A4988 未验证](BUG-20260610-004-motor-unverified.md) | P1 | 06-10 | 缺 12V 适配器 |
| BUG-20260610-005 | [Timer 模式黑屏](BUG-20260610-005-timer-black-screen.md) | P1 | 06-10 | 疑似 DMA 旧 bug 污染，待复测 |
| BUG-20260610-006 | [Settings 模式只读](BUG-20260610-006-settings-readonly.md) | P2 | 06-10 | 单键限制 |
| BUG-20260610-007 | [Timer 无法调整时长](BUG-20260610-007-timer-no-adjust.md) | P2 | 06-10 | 单键限制 |
| BUG-20260610-008 | [4 个编译 Warning](BUG-20260610-008-compiler-warnings.md) | P3 | 06-10 | em dash + 枚举混用，无害 |

## 已修复

| Bug ID | 标题 | 等级 | 发现 | 修复 | 文件 |
|--------|------|------|------|------|------|
| BUG-20260610-003 | [SHT30 温湿度传感器未接](BUG-20260610-003-sht30-missing.md) | P1 | 06-10 | 06-21 | `temp_sensor.c`（驱动已就绪），`temp_mode.c` |
| BUG-20260621-001 | [GCC nano.specs 浮点 printf/scanf 被禁](BUG-20260621-001-gcc-nanospecs-float.md) | P1 | 06-21 | 06-22 | `Makefile` |
| BUG-20260621-002 | [CubeMX 重新生成丢失 USART2](BUG-20260621-002-cubemx-usart2-lost.md) | P1 | 06-21 | 06-22 | `main.c`, `hal_msp.c` |
| BUG-20260617-001 | [fs_config_save Flash 空间泄漏](BUG-20260617-001-flash-space-leak.md) | P1 | 06-17 | 06-17 | `fs_mgr.c` |
| BUG-20260617-002 | W25Q64 Flash 实际已接（原误标为未接） | P1 | 06-10 | 06-17 | 硬件确认，非代码修复 |
| BUG-20260616-004 | 14-31 号运行时 bug（18 个） | P1 | 06-16 | 06-16 | 多文件，详见 `docs/superpowers/runtime-bugs.md` |
| BUG-20260616-003 | 全屏 40KB DMA 刷新 → 脏矩形局部刷新 | P1 | 06-16 | 06-16 | `st7735.c`, `gui.c` |
| BUG-20260616-002 | 多按键 → 单键适配 | P1 | 06-16 | 06-16 | `mode_manager.c`, 各 mode |
| BUG-20260616-001 | TFT GRAM 未同步（init 后） | P1 | 06-16 | 06-16 | `st7735.c` |
| BUG-20260615-002 | SPI1 时钟源算错（APB2 非 APB1） | P1 | 06-15 | 06-15 | `pin_config.h` |
| BUG-20260615-001 | SPI 时钟 21MHz 超规格 → 10.5MHz | P0 | 06-15 | 06-15 | `st7735.c`, `pin_config.h` |

## 不予修复

| Bug ID | 标题 | 等级 | 原因 |
|--------|------|------|------|
| - | - | - | - |

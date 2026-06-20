# Timer 模式黑屏

- **Bug ID**：BUG-20260610-005
- **严重等级**：P1-严重
- **发现日期**：2026-06-10
- **修复日期**：—

## 现象描述
切换到 Timer 模式时屏幕全黑，不显示倒计时界面。

## 复现步骤
1. 短按按键从 Clock → Temp → Timer
2. 屏幕保持黑色（或有残留画面）

## 根因分析
单键适配过程中疑似 DMA 旧 bug 污染。HANDOFF.md 记录"待复测"。

## 修复方案
Keil 关闭 → 重新打开 → Rebuild → 重新 ISP 烧录后验证是否复现。

## 影响文件
- `firmware/App/modes/timer_mode.c`

## 验证方式
Keil 重开重编译烧录后，切换到 Timer 模式观察 TFT 是否正常显示进度环和时间数字

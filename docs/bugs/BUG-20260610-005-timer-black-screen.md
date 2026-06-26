# Timer 模式黑屏

- **Bug ID**：BUG-20260610-005
- **严重等级**：P1-严重
- **发现日期**：2026-06-10
- **修复日期**：2026-06-23

## 现象描述
切换到 Timer 模式时屏幕全黑，不显示倒计时界面。番茄钟和呼吸引导模式也有同样黑屏问题。

## 复现步骤
1. 短按按键从 Clock → Temp → Timer
2. TFT 全黑，无进度环、时间数字、状态标签
3. 长按开始倒计时后依然黑屏

## 根因分析
NOT DMA 旧 bug 污染（旧分析错误）。真实原因是 `timer_mode_enter()` 没有重置 `render_cache`。render_cache 初始值 `{0xFFFFFFFF, 0, TIMER_STOPPED}`，首次 enter 时缓存与当前状态不一致但去重逻辑误判"无变化"跳过渲染。

同样问题存在于 `pomodoro_mode_enter()`。

## 修复方案
在 enter() 中强制重置 render_cache：
- `timer_mode_enter()`: `render_cache.remaining = 0xFFFFFFFF; render_cache.st = TIMER_STOPPED;`
- `pomodoro_mode_enter()`: `render_cache.remaining = 0xFFFFFFFF; render_cache.sessions = 0xFF;`

## 影响文件
- `firmware/App/modes/timer_mode.c`
- `firmware/App/modes/pomodoro_mode.c`

## 验证方式
切换 Timer/Pomodoro 模式 → TFT 正常显示进度环 + 数字 + 状态标签

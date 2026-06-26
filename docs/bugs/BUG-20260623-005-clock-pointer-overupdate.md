# clock_mode 每秒更新指针角度导致电机频繁微动

- **Bug ID**：BUG-20260623-005
- **严重等级**：P2-轻微
- **发现日期**：2026-06-23
- **修复日期**：2026-06-23

## 现象描述
电机在时钟模式下每几秒就微微转动一次。24h 制下每秒指针角度变化仅 0.25°，换算成 16 微步只有 2.2 步/秒——diff 不到 10 步阈值，但 pointer_engine_update() 平滑插值累积偏差后偶尔触发一次电机微动。

## 根因分析
`clock_mode_update()` 每 100ms 调用 `pointer_set_clock_24h()`，每秒更新 10 次目标角度。虽然单次变化极小（0.25°/s），但指针引擎插值算法使得 current_angle 持续追赶 target_angle，在分钟边界累积出超过 10 步的 diff，触发电机。

## 修复方案
加入半小时桶判断：
```c
uint8_t bucket = hours * 2 + minutes / 30;
if (bucket == last_bucket) return;  // 同桶跳过
```
每 30 分钟更新一次角度，桶内不重复调用 `pointer_set_clock`。

## 影响文件
- `firmware/App/modes/clock_mode.c`

## 验证方式
烧录后电机大部分时间处于停机断电态，半小时一次角度更新触发微转（24h 制：30 分钟 = 7.5° = 67 微步，diff 足够大，一次转完到位断电）

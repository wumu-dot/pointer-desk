# GCC nano.specs 默认禁用浮点 printf/scanf——日志空白 + 天气数据丢失

- **Bug ID**：BUG-20260621-001
- **严重等级**：P1-严重
- **发现日期**：2026-06-21
- **修复日期**：2026-06-22

## 现象描述
Keil → STM32CubeCLT (GCC) 迁移后：
1. 串口日志中 `Pointer target set:  deg` 角度值为空（本应是 `354.2 deg`）
2. ESP32 天气数据帧收到但 `sscanf("%f")` 静默失败，`g_weather` 永远不更新，天气主页显示 "Waiting ESP32..."
3. RTC 时间不会从 ESP32 帧中自动校准

## 复现步骤
1. 用 CubeMX 生成 Makefile + GCC 编译
2. 烧录运行，观察串口日志
3. 角度显示为空，天气数据不更新

## 根因分析
GCC `-specs=nano.specs`（CubeMX Makefile 默认选项）裁剪掉了 `printf`/`scanf` 家族的浮点格式化支持。ARMCC 没有这个行为。

- `snprintf(buf, sizeof(buf), "%.1f", angle)` → 输出空字符串
- `sscanf(frame, "%f", &temp)` → 返回匹配数不足 9，函数静默 return

## 修复方案
Makefile LDFLAGS 加上：
```
-u _printf_float    ← 恢复 printf 浮点
-u _scanf_float     ← 恢复 scanf 浮点
```
代价：代码段增加 ~10KB。完整命令：
```
LDFLAGS = $(MCU) -specs=nano.specs -u _printf_float -u _scanf_float -T$(LDSCRIPT) ...
```

## 影响文件
- `firmware/Makefile`

## 验证方式
串口日志中 `Pointer target set: 354.2 deg (mode 0)` 角度数字正常显示，天气帧 `weather frame: 2026-06-22 01:10:50|23.0|47|Partly_Cloudy|96` 解析后屏幕显示 23.0°C/47%。

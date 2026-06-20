# 4 个编译 Warning（ARMCC V5）

- **Bug ID**：BUG-20260610-008
- **严重等级**：P3-轻微
- **发现日期**：2026-06-10
- **修复日期**：—

## 现象描述
Keil ARMCC V5 编译输出 4 个 Warning，不影响运行。

## 复现步骤
1. Keil → Rebuild
2. Build Output 显示 0 Error, 4 Warning

## 根因分析

| 文件 | 行 | Warning # | 原因 |
|------|-----|-----------|------|
| `w25q64.c` | 237 | #870-D | LOG 字符串含 em dash `—`（UTF-8 多字节字符） |
| `mode_manager.c` | 69 | #188-D | `mode_id_t i = 0` 枚举类型与整数混用 |
| `mode_manager.c` | 81 | #870-D | LOG 字符串含 em dash `—` |
| `mode_manager.c` | 141 | #188-D | `mode_id_t next = (current_mode + 1)` 整数赋值给枚举 |

## 修复方案
- #870-D：替换 em dash `—` 为 `--`
- #188-D：加显式类型转换 `(mode_id_t)`

无害，可后续顺手修。

## 影响文件
- `firmware/Drivers/BSP/flash/w25q64.c:237`
- `firmware/App/mode_manager.c:69,81,141`

## 验证方式
修复后 Rebuild → 0 Error, 0 Warning

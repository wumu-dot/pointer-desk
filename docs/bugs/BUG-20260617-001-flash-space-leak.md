# fs_config_save Flash 空间泄漏

- **Bug ID**：BUG-20260617-001
- **严重等级**：P1-严重
- **发现日期**：2026-06-17
- **修复日期**：2026-06-17

## 现象描述
每次退出 Settings 模式保存配置时，`fs_config_save` 将 brightness/temp_unit/timer_default 写到新 Flash 地址而非覆盖原位置。串口日志显示地址持续递增（0x1120→0x1126→0x112C…），每次偏移 +6 字节。

## 复现步骤
1. 上电 → 进入 Settings → 退出（触发保存）
2. 重复 5 次
3. 观察串口日志 `fs_config_save` 地址单调递增，永不回落

## 根因分析
`fs_config_save()` 无条件使用 `g_next_data_offset` 作为写入地址（append-only 策略）。`find_entry()` 正确找到了已存在的 key，但仍写到队尾，旧数据原地废弃。`g_next_data_offset` 只进不退。

## 修复方案
三条分支：
- 新 key → 追加到队尾，推进 `g_next_data_offset`
- 已存在且 `size ≤ 旧 size` → **原地覆写**，不推进偏移
- 已存在且 `size > 旧 size` → 追加到队尾，推进偏移

空间检查仅对追加写入场景执行。LOG 追加 `[new]`/`[in-place]`/`[grew]` 标记。

提交：`06d7917 fix(fs_mgr): Flash 空间泄漏 — fs_config_save 由强制追加改为原地覆写`

## 影响文件
- `firmware/Middleware/fs/fs_mgr.c:210-275`

## 验证方式
反复进出 Settings 5 次，观察串口日志 `fs_config_save` 地址不再递增，`[in-place]` 标记出现

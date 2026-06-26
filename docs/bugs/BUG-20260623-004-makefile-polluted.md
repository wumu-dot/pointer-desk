# Makefile 被 HTML 简历代码污染

- **Bug ID**：BUG-20260623-004
- **严重等级**：P2-轻微
- **发现日期**：2026-06-23
- **修复日期**：2026-06-23

## 现象描述
`mingw32-make` 报大量语法错误，打开 Makefile 发现开头 `# target` 后跟入了大量 HTML 简历代码（`<!DOCTYPE html>...`），覆盖了 Makefile 原始内容。

## 根因分析
生成 resume.docx 时，某次写入操作误将 HTML 代码写入到了 Makefile 文件中。

## 修复方案
`git show 85e3b8a:firmware/Makefile > firmware/Makefile` 恢复干净版本，再手动补回 pomodoro_mode.c / breathe_mode.c 源文件条目。

## 影响文件
- `firmware/Makefile`

## 验证方式
`mingw32-make clean && mingw32-make -j6` 编译 0 Error

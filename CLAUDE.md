# OV-Watch AI 开发规范

> 每次 Claude Code 打开此项目时自动加载。修改保存后下次对话生效。

---

## 核心原则

```
正确性  >  速度
可读性  >  巧妙
可维护性 > 极致优化
小改动  > 大重构
显式    > 隐式
```

- 不要猜测需求
- 不要创造不存在的接口
- 不要修改无关代码
- 不要重构用户未要求的内容
- 发现问题先分析再修改

---

## 任务执行流程

```
分析问题 → 制定计划 → 等待确认 → 实施修改 → 验证结果 → 输出总结
```

**关键约束：** 未经用户确认，不得自行扩大修改范围。

---

## 修改代码前必须

1. 阅读完整文件
2. 阅读相关依赖
3. 分析调用链
4. 再进行修改

**禁止：**
- 只看局部代码直接改
- 改 A 炸 B 影响 C

**原则：**
- 做最小可能的改动
- 不重构无关代码
- 保持向后兼容
- 永不删除现有功能

---

## 代码质量

- 单一职责函数（< 50 行）
- 避免深层嵌套（> 4 层）
- 无硬编码值（用宏或配置）
- 错误显式处理
- C 语言：函数前加注释说明功能、参数、返回值

---

## Git 提交格式

```
feat: 新功能
fix:  Bug 修复
refactor: 重构
docs: 文档
chore: 杂务
```

示例：`fix(tft): spi clock corrected from 21MHz to 10.5MHz`

---

## OV-Watch 项目专属规则

### 编译环境
- IDE：Keil MDK V5 + ARMCC V5
- 编译完运行 `bash .claude/scripts/check-firmware.sh` 验证 18 项检查
- ARMCC V5 不支持 `0b` 二进制字面量、不支持 C++ 风格注释混用

### 烧录方式
- **仅 ISP 串口下载**，无 ST-Link/SWD
- 烧录引脚和调试串口共用 USART1 (PA9/PA10)

### 引脚修改
- **修改任何引脚前必须先看 `Core/Src/stm32f4xx_hal_msp.c` 确认 CubeMX 实际配置**
- `pin_config.h` 的宏值必须与 `hal_msp.c` 一致
- 历史上5个引脚因未校验 CubeMX 而写错

### 外设总线
- SPI1 (TFT)：APB2 84MHz，分频后 ≤15MHz
- SPI2 (Flash)：APB1 42MHz
- I2C2 (SHT30)：PF0/PF1，非 PB10/PB11

### 相关文档
- 完整交接：`HANDOFF.md`
- Bug 清单：`BUGS.md`
- 回归检查：`.claude/scripts/check-firmware.sh`

### Skill 调用场景

| 触发条件 | 调用 Skill |
|----------|-----------|
| 实现新功能/多文件改动前 | `brainstorming` → 讨论设计，确认后再动手 |
| 设计确定后要写计划 | `writing-plans` → 生成分步实现计划 |
| 计划确定后要写代码 | `subagent-driven-development` 或 `executing-plans` |
| 遇到 Bug/测试失败 | `systematic-debugging` → 系统化排查，不要直接猜 |
| 写完代码/修完 Bug | `verification-before-completion` → 编译+回归检查，确认通过再声称完成 |
| 开发分支写完 | `finishing-a-development-branch` → 合并/PR/清理 |
| 用户说"提交代码" | `commit-commands:commit` |
| 用户说"提交并推送" | `commit-commands:commit-push-pr` |
| 收到别人代码审查意见 | `receiving-code-review` → 技术评估后再改 |
| 要审查当前改动 | `code-review` → 审查 bugs/简化/效率 |
| 开始新功能需要隔离环境 | `using-git-worktrees` |
| 要写测试 | `test-driven-development` |
| 依赖安全审计 | `dependency-audit` |

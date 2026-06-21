# CubeMX 重新生成 Makefile 后 USART2 初始化丢失——ESP32 天气桥接失效

- **Bug ID**：BUG-20260621-002
- **严重等级**：P1-严重
- **发现日期**：2026-06-21
- **修复日期**：2026-06-22

## 现象描述
CubeMX 将 Toolchain 从 MDK-ARM 切换为 Makefile 并 GENERATE CODE 后，ESP32 天气数据完全不接收，TFT 显示 "Waiting ESP32..."，串口日志无 `weather frame:` 行。

## 复现步骤
1. CubeMX 打开 `ov-watch.ioc` → Toolchain 改为 Makefile → GENERATE CODE
2. GCC 编译烧录
3. ESP32 数据帧不出现

## 根因分析
USART2 是手动添加的外设（不在 CubeMX `.ioc` 配置中）。CubeMX 重新生成代码时只生成 `.ioc` 中已配置的外设，不知道 USART2 的存在。三处代码被删除：
- `main.c`：`MX_USART2_UART_Init()` 函数定义 + 调用被删除
- `hal_msp.c`：`HAL_UART_MspInit` 中的 `else if(huart->Instance==USART2)` 块被删除（PD5/PD6 GPIO + DMA1_Stream5 初始化）
- `main.c`：`HAL_UART_Receive_DMA(&huart2, ...)` 调用在 `USER CODE` 保护区内未丢

## 修复方案
从 git 历史恢复 `hal_msp.c`，手动补回 `main.c` 的 `MX_USART2_UART_Init()` 函数。

## 影响文件
- `firmware/Core/Src/main.c`
- `firmware/Core/Src/stm32f4xx_hal_msp.c`

## 验证方式
串口日志出现 `weather_bridge DMA poll started`，60 秒内收到 `weather frame: 2026-06-22 xx:xx:xx|...`。

## 预防措施
已更新 `check-firmware.sh` 增加检查项。建议在 `rules.md` 中加入：工具链/构建系统迁移前，必须逐项对比 `.ioc` 配置与实际 `main.c`/`hal_msp.c` 中调用的外设初始化函数。

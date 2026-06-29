# 项目全局上下文（会话自动继承）
## 硬件平台
主控：STM32F407ZGT6（鹿小班 LXB407ZG-P1）| HSE 8MHz | 调试 ST-Link SWD
显示：1.8" TFT ST7735S（SPI1 84MHz÷8=10.5MHz）| 存储：W25Q64 Flash（SPI2 42MHz÷16=2.625MHz）
传感器：SHT30 温湿度（I2C2，PF0/PF1）| 电机：A4988（EN→PA2，低有效使能，空闲HIGH断电）
按键：PB6(上)/PB7(下)/PB8(左)/PB9(右) 四键，GPIO内置上拉
无线：ESP32 天气桥接（UART DMA接收）

## 软件环境
RTOS：FreeRTOS（CMSIS-RTOS v2）| 驱动库：STM32 HAL
构建：GCC（mingw32-make -j6，推荐）/ Keil MDK V5+ARMCC V5（兼容保留）
烧录：build_and_flash.bat（ST-Link SWD，唯一可用）
调试：openocd_debug.bat（OpenOCD+GDB端口3333）

## 关键文件速查
| 文件 | 职责 |
|------|------|
| firmware/Core/Src/main.c | 固件入口，FreeRTOS启动 |
| firmware/Core/Inc/pin_config.h | 所有引脚宏定义 |
| firmware/Core/Src/stm32f4xx_hal_msp.c | CubeMX引脚配置（改引脚前必读） |
| firmware/App/mode_manager.c | 模式状态机（6模式）+按键分发 |
| firmware/Middleware/pointer/pointer_engine.c | 指针引擎（角度映射、平滑插值） |
| firmware/Drivers/BSP/motor/a4988.c | A4988步进电机驱动（TIM2 PWM） |

## 开发硬性边界
1. 禁止修改 firmware/Core/、firmware/Drivers/ 底层库
2. 修改任何引脚前必看 hal_msp.c，pin_config.h宏值必须与之一致
3. CubeMX重新生成后必须对比差异，手动恢复丢失外设
4. 时钟指针每30分钟更新，电机位置伺服diff>10步才使能
5. ARMCC V5不支持0b二进制字面量

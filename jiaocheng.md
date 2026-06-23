# 脚本说明
| 脚本 | 功能 | 用法 |
|------|------|------|
| build_and_flash.bat | 编译 + ST-Link SWD 烧录 | 双击（需要 ST-Link） |
| flash.bat | 编译 + ISP 串口烧录 | 双击（需手动 BOOT0 跳线） |
| openocd_debug.bat | OpenOCD + GDB 调试 | 双击（需要 ST-Link） |
| gdb_debug.bat | ST-Link GDB Server 调试 | 双击（备用） |



# 调试
## 1. codergraph + OpenOCD + GDB 调试

# CodeGraph + OpenOCD/GDB 嵌入式调试实战教程

## 适用场景

- 嵌入式项目（STM32/ESP32/RT-Thread/FreeRTOS等）
- 代码是 AI 生成的，不知道函数名和中断处理函数
- 硬件跑着跑着 HardFault 或 看门狗复位

---

## 第零步（可选）：一键启动脚本（Windows）

项目根目录下提供了 4 个批处理脚本，**双击即可**完成编译、烧录、调试，无需手动敲命令：

| 脚本 | 功能 | 用法 |
|------|------|------|
| `build_and_flash.bat` | 编译 + ST-Link SWD 烧录 | 双击（需要 ST-Link） |
| `flash.bat` | 编译 + ISP 串口烧录 | 双击（需手动 BOOT0 跳线） |
| `openocd_debug.bat` | OpenOCD + GDB 调试 | 双击（需要 ST-Link） |
| `gdb_debug.bat` | ST-Link GDB Server 调试 | 双击（备用） |

> 💡 **提示**：如果双击后窗口一闪而过，请右键编辑 `.bat` 文件，在最后一行加上 `pause`，方便查看报错信息。

---

## 第一步：生成嵌入式项目的"地图"

进入你的固件源码目录：

```bash
cd /path/to/your/firmware
codegraph init
```

**关键区别**：嵌入式项目通常有 startup.s、链接脚本、中断向量表。CodeGraph 能把这些也扫描进去，帮你找到：

- 中断服务函数（USART1_IRQHandler 等）
- 启动入口（Reset_Handler）
- RTOS 的任务函数

---

## 第二步：用自然语言搜出嵌入式关键函数

在 Cursor/Claude Code 里问：

**🔍 场景1：HardFault 了，不知道哪里崩**

> "找出所有可能触发 HardFault 的操作，比如指针访问、数组越界、堆栈溢出的相关函数"

**🔍 场景2：中断没响应**

> "USART1 的中断服务函数叫什么？谁初始化了它？"

**🔍 场景3：RTOS 任务**

> "列出所有 FreeRTOS 任务函数，以及它们的入口函数名"

**🔍 场景4：外设初始化**

> "GPIO、ADC、TIM2 的初始化函数分别在哪个文件哪一行？"

---

## 第三步：OpenOCD + GDB 连接目标板

```bash
# 终端1：启动 OpenOCD（以 ST-Link 为例）
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

# 终端2：连接 GDB
arm-none-eabi-gdb ./build/firmware.elf
(gdb) target remote localhost:3333
(gdb) monitor reset halt          # 复位并暂停
(gdb) load                         # 烧录固件
```

> ⚠️ 如果你在 Windows 下使用了上面的 `openocd_debug.bat`，这一步可以跳过，脚本会自动帮你连接。

---

## 第四步：把 CodeGraph 搜到的函数名变成断点

### 单个断点

```bash
(gdb) break USART1_IRQHandler
(gdb) break main.c:127             # 或指定行号
```

### 批量断点（把 CodeGraph 返回的清单快速下断）

假设 CodeGraph 返回了 5 个可疑函数：

```bash
# 在 GDB 中逐条敲，或提前写成脚本
break parse_command
break process_packet
break dma_transfer_complete
break uart_rx_callback
break hardfault_handler
```

### 嵌入式专用：断点加条件（避免狂按 c）

```bash
# 只在特定外设状态异常时停下
(gdb) break USART1_IRQHandler if USART1->SR & 0x0001
```

---

## 第五步：OpenOCD/GDB 常用调试命令（嵌入式特供）

| 用途 | GDB 命令 | 说明 |
|------|----------|------|
| 继续运行 | `c` | continue（你提到的） |
| 单步执行（不进入函数） | `n` | next，跳过函数内部 |
| 单步执行（进入函数） | `s` | step，钻进去看 |
| 打印变量/寄存器 | `p my_var` 或 `p/x $r0` | print，`p/x` 十六进制 |
| 查看调用栈 | `bt` | 对照 CodeGraph 地图验证 |
| 查看当前汇编 | `disas` | 反汇编当前函数 |
| 查看内存 | `x/4x 0x20000000` | 以十六进制看4个字 |
| 查看寄存器 | `info registers` | 看所有 CPU 寄存器 |
| 监控外设寄存器 | `p/x *(uint32_t*)0x40013800` | 直接读地址 |

---

## 实战案例：UART 收不到数据

### 现象

板子跑起来，串口没反应，怀疑中断没触发。

### Step 1 - CodeGraph 搜

问 AI 助手：

> "这个项目里 UART 相关的函数有哪些？中断处理函数叫什么？"

返回：

```
UART 初始化: uart_init (src/uart.c:34)
中断处理: USART2_IRQHandler (src/stm32_it.c:78)
回调函数: uart_rx_callback (src/uart.c:102)
调用关系: uart_init → HAL_UART_Init → HAL_NVIC_SetPriority
```

### Step 2 - GDB 下断验证

```bash
(gdb) break uart_init
(gdb) break USART2_IRQHandler
(gdb) c
```

### Step 3 - 追踪

- 第一个断点停在了 uart_init，看参数发现中断优先级配置错了
- 用 `p/x NVIC->ISER` 检查中断是否真的使能了
- 修正后，第二个断点正常触发，问题解决

---

## 嵌入式专用：监控内存地址变化

如果怀疑某块内存被意外篡改（比如 DMA 越界）：

```bash
(gdb) watch *(uint32_t*)0x20001000
(gdb) c
```

一旦该地址被写入，GDB 会自动停下，配合 `bt` 看是谁干的。

---

## 常用命令速查表（嵌入式特供）

| 目的 | CodeGraph 搜索词 | GDB 命令 |
|------|------------------|----------|
| 找中断处理函数 | "XXX 外设的中断函数叫什么" | `break <函数名>` |
| 找初始化函数 | "XXX 外设在哪个文件初始化" | `break <函数名>` |
| 找 RTOS 任务 | "列出所有任务函数" | - |
| 查调用链 | "谁调用了 DMA_Start" | `bt` |
| 看外设寄存器 | - | `p/x *(uint32_t*)0x400XXXXX` |
| 看当前 PC 在哪 | - | `info registers pc` |
| 反汇编验证 | - | `disas <函数名>` |

---

## 避坑指南（嵌入式特供）

1. **编译时一定要加 `-g` 和 `-O0`**：`-O2` 优化会让 GDB 行号错乱，断点漂移
2. **OpenOCD 连不上**：检查调试器驱动、目标板供电、SWD/JTAG 接线
3. **断点打不上**：确认 `.elf` 文件是带调试符号的版本，不是 `.bin` 或 `.hex`
4. **Watchpoint 有限**：硬件 watchpoint 通常只有 2-4 个，别贪多
5. **RTOS 任务切换**：用 `info threads` 和 `thread <id>` 切换任务上下文查看

---

## 进阶：Linux/WSL 一键调试脚本（备用）

如果你在 Linux 或 WSL 环境下开发，也可以用这个 `bash` 脚本实现自动化：

```bash
#!/bin/bash
# 文件名: debug.sh

# 1. 用 CodeGraph 把所有函数导出到文件
codegraph list-functions > /tmp/funcs.txt

# 2. 生成 GDB 断点脚本（只取前 20 个，避免太慢）
head -20 /tmp/funcs.txt | sed 's/^/break /' > /tmp/gdb_init.txt

# 3. 启动 OpenOCD（后台）
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg &

# 4. 启动 GDB 并加载断点脚本
arm-none-eabi-gdb -x /tmp/gdb_init.txt ./build/firmware.elf
```

---

## 总结

| 工具 | 角色 | 核心动作 |
|------|------|----------|
| CodeGraph | 🗺️ 地图 | 自然语言搜出函数名、调用链、外设初始化位置 |
| OpenOCD | 🔌 连接线 | 让 GDB 和硬件芯片对话 |
| GDB | 🔍 放大镜 | 下断点、看寄存器、查内存、单步走（c/n/s） |

**心法不变**：地图指哪儿，断点打哪儿。不用再靠 objdump 或翻启动文件找中断函数名了。

---

> 如果你能告诉我：芯片型号（STM32F4？ESP32？）、调试器（ST-Link？J-Link？CMSIS-DAP？）、RTOS（FreeRTOS？RT-Thread？裸机？），我可以把这个教程再细化成你项目专用的一键脚本和命令模板。😊
```

---

**改动说明**：  
1. **新增“第零步”**：把你的 4 个 `.bat` 脚本表格放在最前面，Windows 用户直接双击就能用。  
2. **第三步末尾加了提示**：提醒 Windows 用户如果用了脚本，可以跳过手动连接步骤。  
3. **原来的 Bash 一键脚本**移到“进阶”部分，并标注为 Linux/WSL 备用，避免和 Windows 的 `.bat` 混淆。

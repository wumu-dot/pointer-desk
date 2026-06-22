# OV-Watch GDB 初始化脚本
# 启动方式: arm-none-eabi-gdb build/ov-watch.elf
# 前提: ST-LINK_gdbserver.exe 已在另一个终端运行

set confirm off
set pagination off

# 连接 ST-Link GDB Server (ST 官方默认端口 61234)
target remote :61234

# 复位暂停
monitor reset halt

# 烧录固件
load

# 在 main 入口设断点
break main

echo === GDB ready. 'c'=run, 'n'=next, 's'=step, 'p var'=print ===\n

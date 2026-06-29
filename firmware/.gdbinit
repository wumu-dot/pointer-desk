set confirm off
set pagination off
target extended-remote :3333
monitor reset halt
load
break main
echo --- Ready. Type 'c' to run. ---\n

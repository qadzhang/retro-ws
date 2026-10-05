# hello.py - CPython 示例（仅 ESP32-S3 N16R8）/ CPython example (ESP32-S3 only)
# 运行 / run:  script run /sdcard/scripts/python/hello.py

import retro_ui

def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

print("CPython on ESP32 Retro WS")
for i in range(11):
    print(f"fib({i}) = {fib(i)}")

# retro_ui 胶水层 / retro_ui glue layer
retro_ui.msgbox("Python", "你好，CPython!")
choice = retro_ui.list("选择", "演示项目:", ["螺旋", "彩蛋", "退出"])
if choice == 2:
    retro_ui.status("退出演示")

# hello.be - Berry 示例 / Berry example
# 运行 / run:  script run /sdcard/scripts/berry/hello.be

# 斐波那契 / Fibonacci
var fib = def (n)
    if n < 2 return n end
    return fib(n - 1) + fib(n - 2)
end

print("Berry on ESP32 Retro WS")
for i : 0..10
    print("fib(" + str(i) + ") = " + str(fib(i)))
end

# retro_ui 胶水层 / retro_ui glue layer
retro_ui_msgbox("Berry", "你好，Berry!")
var name = retro_ui_input("输入", "你的名字:", 32)
if name != ""
    retro_ui_msgbox("你好", name + ", 欢迎使用复古工作站!")
end

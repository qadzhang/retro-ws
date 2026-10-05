// hello.js - Duktape JavaScript 示例程序 / Duktape JavaScript Example Program
// 功能: 打印Hello World, 斐波那契, 猜数字游戏
// Features: Hello World, Fibonacci, Guess Number Game

// ------------------------------------------------------------
// 打印分隔线 / Print Separator
// ------------------------------------------------------------
function printLine() {
    print("================================");
}

// ------------------------------------------------------------
// Hello World / 你好世界
// ------------------------------------------------------------
printLine();
print(" ESP32-S3 Retro System");
print(" JavaScript Example Program");
printLine();
print();

// ------------------------------------------------------------
// 变量和运算 / Variables and Operations
// ------------------------------------------------------------
var a = 10;
var b = 20;
var c = a + b;
print("Variables: a = " + a + ", b = " + b);
print("a + b = " + c);
print();

// ------------------------------------------------------------
// 斐波那契数列 / Fibonacci Sequence
// ------------------------------------------------------------
function fibonacci(n) {
    var fibs = [];
    var a = 1, b = 1;
    for (var i = 0; i < n; i++) {
        fibs.push(a);
        var temp = a + b;
        a = b;
        b = temp;
    }
    return fibs;
}

print("Fibonacci Sequence (1-15):");
var fibs = fibonacci(15);
print(fibs.join(", "));
print();

// ------------------------------------------------------------
// 数组和对象 / Arrays and Objects
// ------------------------------------------------------------
print("Array Demo:");
var fruits = ["Apple", "Banana", "Orange", "Grape"];
for (var i = 0; i < fruits.length; i++) {
    print("  " + (i + 1) + ": " + fruits[i]);
}

print();
print("Object Demo:");
var person = {
    name: "ESP32",
    version: "S3",
    cores: 2,
    frequency: "240MHz"
};
var keys = Object.keys(person);
for (var i = 0; i < keys.length; i++) {
    var key = keys[i];
    print("  " + key + ": " + person[key]);
}
print();

// ------------------------------------------------------------
// 猜数字游戏 / Guess Number Game
// ------------------------------------------------------------
printLine();
print(" Guess Number Game");
printLine();
print("I'm thinking of a number between 1 and 100.");
print("You have 7 tries to guess it!");
print();

// 简化的随机数 (NuttX/Duktape 环境可能没有 Math.random)
// Simplified random (Math.random may not be available in NuttX/Duktape)
var secret = 42;  // 固定数字便于演示 / Fixed for demo
var tries = 0;

// ------------------------------------------------------------
// 注意: 由于Nuttx控制台输入限制，猜数字游戏需要外部修改secret值测试
// Note: Due to NuttX console input limitations, 
// the game needs external modification to test
// ------------------------------------------------------------

// 模拟猜测过程 / Simulate guessing process
print("Simulating guessing process...");
print();

for (var guess = 35; guess <= 45; guess++) {
    tries++;
    print("Guess #" + tries + ": " + guess + " - ");
    
    if (guess > secret) {
        print("Too high!");
        if (tries >= 7) break;
    } else if (guess < secret) {
        print("Too low!");
        if (tries >= 7) break;
    } else {
        print("Correct!");
        print();
        print("You got it in " + tries + " tries!");
        break;
    }
}

if (tries > 7 || secret < 35 || secret > 45) {
    print("Game over! The number was " + secret);
}

print();
printLine();
print("Program finished!");
print("Thank you for playing!");
printLine();

// ------------------------------------------------------------
// 附加: 阶乘计算 / Bonus: Factorial Calculation
// ------------------------------------------------------------
print();
print("Bonus: Factorial Calculation");

function factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

for (var i = 1; i <= 10; i++) {
    print("  " + i + "! = " + factorial(i));
}

print();
print("All done!");

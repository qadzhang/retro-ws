REM "hello.bas - my-basic 示例程序 / my-basic Example Program"
REM "功能: 打印 Hello World, 计算斐波那契, 演示循环和条件判断"
REM "Features: Print Hello World, Calculate Fibonacci, Loop and Condition Demo"

REM ========================================
REM ESP32-S3 Retro System - my-basic Demo
REM ========================================

PRINT ""
PRINT "================================"
PRINT " ESP32-S3 Retro System"
PRINT " my-basic Example Program"
PRINT "================================"
PRINT ""

REM 打印 Hello World / Print Hello World
PRINT "Hello, World!"
PRINT "你好, 世界!"
PRINT ""

REM 变量演示 / Variable Demo
A = 10
B = 20
C = A + B
PRINT "Variables: A ="; A; ", B ="; B
PRINT "A + B ="; C
PRINT ""

REM 循环演示 - 计算斐波那契 / Loop Demo - Fibonacci
PRINT "Fibonacci Sequence (1-10):"
N = 1
F1 = 1
F2 = 1
PRINT F1; ","; F2;
FOR I = 3 TO 10
  F = F1 + F2
  PRINT ","; F;
  F1 = F2
  F2 = F
NEXT I
PRINT ""
PRINT ""

REM 条件判断 / Condition Demo
PRINT "Condition Demo:"
IF A < B THEN PRINT "A is less than B"
IF A = B THEN PRINT "A equals B"
IF A > B THEN PRINT "A is greater than B"
PRINT ""

REM 演示 FOR 循环 / FOR Loop Demo
PRINT "FOR Loop Demo (1-5):"
FOR I = 1 TO 5
  PRINT I;
  IF I < 5 THEN PRINT ",";
NEXT I
PRINT ""
PRINT ""

REM 演示 WHILE 循环 / WHILE Loop Demo
PRINT "WHILE Loop Demo:"
X = 1
WHILE X <= 5
  PRINT X;
  X = X + 1
  IF X <= 5 THEN PRINT ",";
WEND
PRINT ""
PRINT ""

REM 演示字符串 / String Demo
A$ = "Hello"
B$ = "my-basic"
C$ = A$ + " " + B$
PRINT "String Demo:"
PRINT "A$ = "; A$
PRINT "B$ = "; B$
PRINT "C$ = "; C$
PRINT ""

REM 数组演示 / Array Demo
DIM A(5)
FOR I = 1 TO 5
  A(I) = I * I
NEXT I
PRINT "Array Demo: A(1..5) = squares"
FOR I = 1 TO 5
  PRINT "A("; I; ") = "; A(I)
NEXT I
PRINT ""

REM 内置函数演示 / Built-in Functions Demo
PRINT "Built-in Functions Demo:"
PRINT "ABS(-5) = "; ABS(-5)
PRINT "INT(3.7) = "; INT(3.7)
PRINT "RND = "; RND
PRINT "SQR(16) = "; SQR(16)
PRINT ""

REM 演示 INPUT 语句 / INPUT Statement Demo
PRINT "INPUT Demo:"
PRINT "Enter a number (or press ENTER to skip):"
PRINT "(This demo uses a preset value)"
INPUT "Your number: ", N
PRINT "You entered: "; N
PRINT ""

PRINT "================================"
PRINT "Program finished!"
PRINT "Thank you for playing!"
PRINT "================================"

END

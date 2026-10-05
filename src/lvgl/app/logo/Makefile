# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# Makefile for Logo Turtle Graphics Interpreter
# 用于 PC 平台测试的 Makefile
#

# 目标平台 / Target platform
HOST ?= x86_64-linux-gnu

# 编译器 / Compiler
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g
CFLAGS += -std=c99 -D_POSIX_C_SOURCE=200809L

# 源文件 / Source files
LOGO_SRC = logo_turtle.c logo_vm.c
LOGO_HDR = logo_turtle.h

# 测试程序 / Test program
TEST_SRC = logo_test.c
TEST_OBJ = $(TEST_SRC:.c=.o)
LOGO_OBJ = $(LOGO_SRC:.c=.o)

# 输出 / Output
TEST_BIN = logo_test

# 默认目标 / Default target
all: $(TEST_BIN)

# 编译测试程序 / Build test program
$(TEST_BIN): $(LOGO_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $(LOGO_OBJ) $(TEST_OBJ) -lm

# 编译源文件 / Compile source files
%.o: %.c $(LOGO_HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理 / Clean
clean:
	rm -f $(LOGO_OBJ) $(TEST_OBJ) $(TEST_BIN)

# 测试 / Test
test: $(TEST_BIN)
	./$(TEST_BIN)

# 显示帮助 / Show help
help:
	@echo "Logo Turtle Graphics Interpreter Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build test program (default)"
	@echo "  test     - Build and run tests"
	@echo "  clean    - Remove built files"
	@echo "  help     - Show this help"
	@echo ""
	@echo "Usage:"
	@echo "  make        # Build test program"
	@echo "  make test   # Build and run tests"
	@echo "  make clean  # Clean build files"

.PHONY: all clean test help

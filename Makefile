CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc
LDFLAGS = -lm -lpthread -lffi -ldl
SRC_DIR = src
BUILD_DIR = build

# Source files from src/ and src/parser/ and src/interpreter/ and src/interpreter/builtins/ and src/interpreter/io/ and src/interpreter/runtime/
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/parser/*.c) $(wildcard $(SRC_DIR)/interpreter/*.c) $(wildcard $(SRC_DIR)/interpreter/builtins/*.c) $(wildcard $(SRC_DIR)/interpreter/io/*.c) $(wildcard $(SRC_DIR)/interpreter/runtime/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = hemlock

# hemfmt-specific sources (exclude main.c and hemfmt.c from shared objects)
FORMATTER_SRCS = $(SRC_DIR)/ast.c $(SRC_DIR)/lexer.c $(SRC_DIR)/formatter.c $(wildcard $(SRC_DIR)/parser/*.c)
FORMATTER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(FORMATTER_SRCS))
HEMFMT_TARGET = hemfmt

all: $(BUILD_DIR) $(BUILD_DIR)/parser $(BUILD_DIR)/interpreter $(BUILD_DIR)/interpreter/builtins $(BUILD_DIR)/interpreter/io $(BUILD_DIR)/interpreter/runtime $(TARGET) $(HEMFMT_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/parser:
	mkdir -p $(BUILD_DIR)/parser

$(BUILD_DIR)/interpreter:
	mkdir -p $(BUILD_DIR)/interpreter

$(BUILD_DIR)/interpreter/builtins:
	mkdir -p $(BUILD_DIR)/interpreter/builtins

$(BUILD_DIR)/interpreter/io:
	mkdir -p $(BUILD_DIR)/interpreter/io

$(BUILD_DIR)/interpreter/runtime:
	mkdir -p $(BUILD_DIR)/interpreter/runtime

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(HEMFMT_TARGET): $(FORMATTER_OBJS) $(BUILD_DIR)/hemfmt.o
	$(CC) $(FORMATTER_OBJS) $(BUILD_DIR)/hemfmt.o -o $(HEMFMT_TARGET) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(HEMFMT_TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@bash tests/run_tests.sh

fmt: $(HEMFMT_TARGET)

.PHONY: all clean run test fmt
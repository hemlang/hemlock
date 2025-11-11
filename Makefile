CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc
SRC_DIR = src
BUILD_DIR = build

# Source files from src/ and src/interpreter/
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/interpreter/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = hemlock

all: $(BUILD_DIR) $(BUILD_DIR)/interpreter $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/interpreter:
	mkdir -p $(BUILD_DIR)/interpreter

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) -lm -lpthread

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@bash tests/run_tests.sh

.PHONY: all clean run test
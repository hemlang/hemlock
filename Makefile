CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc
LDFLAGS = -lm -lpthread -lffi -ldl
SRC_DIR = src
BUILD_DIR = build

# Source files from src/ and src/parser/ and src/interpreter/ and src/interpreter/builtins/ and src/interpreter/io/ and src/interpreter/runtime/
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/parser/*.c) $(wildcard $(SRC_DIR)/interpreter/*.c) $(wildcard $(SRC_DIR)/interpreter/builtins/*.c) $(wildcard $(SRC_DIR)/interpreter/io/*.c) $(wildcard $(SRC_DIR)/interpreter/runtime/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = hemlock

all: $(BUILD_DIR) $(BUILD_DIR)/parser $(BUILD_DIR)/interpreter $(BUILD_DIR)/interpreter/builtins $(BUILD_DIR)/interpreter/io $(BUILD_DIR)/interpreter/runtime $(TARGET)

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

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean: clean-stdlib
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@bash tests/run_tests.sh

# Stdlib C modules (optional - requires external dependencies)
STDLIB_C_DIR = stdlib/c
STDLIB_LWS_WRAPPER = $(STDLIB_C_DIR)/lws_wrapper.so

stdlib: $(STDLIB_LWS_WRAPPER)

$(STDLIB_LWS_WRAPPER): $(STDLIB_C_DIR)/lws_wrapper.c
	@echo "Building libwebsockets wrapper..."
	@mkdir -p $(STDLIB_C_DIR)
	@if $(CC) -shared -fPIC -o $@ $< -lwebsockets $(CFLAGS) 2>&1; then \
		echo "✓ Built $@"; \
	else \
		echo "⚠ Warning: Could not build lws_wrapper.so"; \
		echo "  libwebsockets-dev is not installed or headers not found"; \
		echo "  Install with: sudo apt-get install libwebsockets-dev"; \
		echo "  HTTP/WebSocket tests will be skipped"; \
		exit 0; \
	fi

clean-stdlib:
	rm -f $(STDLIB_LWS_WRAPPER)

.PHONY: all clean run test stdlib clean-stdlib

# Force rebuild of stdlib target
.PHONY: $(STDLIB_LWS_WRAPPER)
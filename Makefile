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

# ========== VALGRIND MEMORY LEAK CHECKING ==========

# Check if valgrind is installed
VALGRIND := $(shell command -v valgrind 2> /dev/null)

# Valgrind flags for leak checking
VALGRIND_FLAGS = --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind-%p.log

# Quick valgrind check on a simple test
.PHONY: valgrind
valgrind: $(TARGET)
ifndef VALGRIND
	@echo "⚠ Valgrind not found. Install with: sudo apt-get install valgrind"
	@exit 1
endif
	@echo "Running valgrind on basic test..."
	@echo "let x = 42; print(x);" > /tmp/valgrind_test.hml
	valgrind $(VALGRIND_FLAGS) ./$(TARGET) /tmp/valgrind_test.hml
	@echo ""
	@echo "Check valgrind-*.log for detailed results"
	@rm -f /tmp/valgrind_test.hml

# Run valgrind on all tests (WARNING: slow, generates many log files)
.PHONY: valgrind-test
valgrind-test: $(TARGET)
ifndef VALGRIND
	@echo "⚠ Valgrind not found. Install with: sudo apt-get install valgrind"
	@exit 1
endif
	@echo "Running valgrind on test suite (this will be slow)..."
	@echo "Note: This generates valgrind-*.log for each test"
	@bash tests/run_tests.sh --valgrind

# Run valgrind with suppressions on a specific file
.PHONY: valgrind-file
valgrind-file: $(TARGET)
ifndef VALGRIND
	@echo "⚠ Valgrind not found. Install with: sudo apt-get install valgrind"
	@exit 1
endif
ifndef FILE
	@echo "Usage: make valgrind-file FILE=path/to/test.hml"
	@exit 1
endif
	@echo "Running valgrind on $(FILE)..."
	valgrind $(VALGRIND_FLAGS) ./$(TARGET) $(FILE)
	@echo ""
	@echo "Check valgrind-*.log for detailed results"

# Valgrind summary: count leaks across all tests
.PHONY: valgrind-summary
valgrind-summary:
	@echo "=== Valgrind Leak Summary ==="
	@if [ -f valgrind-*.log ]; then \
		echo "Analyzing log files..."; \
		grep -h "definitely lost:" valgrind-*.log | awk '{sum+=$$4} END {print "Total definitely lost:", sum, "bytes"}'; \
		grep -h "indirectly lost:" valgrind-*.log | awk '{sum+=$$4} END {print "Total indirectly lost:", sum, "bytes"}'; \
		grep -h "possibly lost:" valgrind-*.log | awk '{sum+=$$4} END {print "Total possibly lost:", sum, "bytes"}'; \
	else \
		echo "No valgrind log files found. Run 'make valgrind' or 'make valgrind-test' first."; \
	fi

# Clean valgrind logs
.PHONY: valgrind-clean
valgrind-clean:
	@echo "Removing valgrind log files..."
	@rm -f valgrind-*.log
	@echo "Done."

.PHONY: all clean run test stdlib clean-stdlib

# Force rebuild of stdlib target
.PHONY: $(STDLIB_LWS_WRAPPER)
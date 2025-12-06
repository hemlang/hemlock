CC = gcc
# Use _DARWIN_C_SOURCE on macOS for BSD types, _POSIX_C_SOURCE on Linux
ifeq ($(shell uname),Darwin)
    CFLAGS = -Wall -Wextra -std=c11 -g -D_DARWIN_C_SOURCE -Iinclude -Isrc
else
    CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc
endif
SRC_DIR = src
BUILD_DIR = build

# Detect libffi, OpenSSL, and libwebsockets (Homebrew on macOS puts them in non-standard locations)
ifeq ($(shell uname),Darwin)
    # On macOS, prefer Homebrew's libffi (system pkg-config points to SDK without headers)
    BREW_LIBFFI := $(shell brew --prefix libffi 2>/dev/null)
    ifneq ($(BREW_LIBFFI),)
        CFLAGS += -I$(BREW_LIBFFI)/include
        LDFLAGS_LIBFFI = -L$(BREW_LIBFFI)/lib
    endif

    # On macOS, also need Homebrew's OpenSSL
    BREW_OPENSSL := $(shell brew --prefix openssl 2>/dev/null)
    ifneq ($(BREW_OPENSSL),)
        CFLAGS += -I$(BREW_OPENSSL)/include
        LDFLAGS_OPENSSL = -L$(BREW_OPENSSL)/lib
    endif

    # On macOS, check for Homebrew's libwebsockets
    BREW_LIBWEBSOCKETS := $(shell brew --prefix libwebsockets 2>/dev/null)
    ifneq ($(BREW_LIBWEBSOCKETS),)
        HAS_LIBWEBSOCKETS := $(shell test -f $(BREW_LIBWEBSOCKETS)/lib/libwebsockets.dylib && echo 1 || echo 0)
        ifeq ($(HAS_LIBWEBSOCKETS),1)
            CFLAGS += -I$(BREW_LIBWEBSOCKETS)/include
            LDFLAGS_LIBWEBSOCKETS = -L$(BREW_LIBWEBSOCKETS)/lib
        endif
    else
        HAS_LIBWEBSOCKETS := 0
    endif
else
    # On Linux, use pkg-config if available
    LIBFFI_CFLAGS := $(shell pkg-config --cflags libffi 2>/dev/null)
    LIBFFI_LIBS := $(shell pkg-config --libs-only-L libffi 2>/dev/null)
    ifneq ($(LIBFFI_CFLAGS),)
        CFLAGS += $(LIBFFI_CFLAGS)
        LDFLAGS_LIBFFI = $(LIBFFI_LIBS)
    endif
    LDFLAGS_OPENSSL =
    LDFLAGS_LIBWEBSOCKETS =

    # Check if libwebsockets is available on Linux
    HAS_LIBWEBSOCKETS := $(shell pkg-config --exists libwebsockets 2>/dev/null && echo 1 || (test -f /usr/include/libwebsockets.h && echo 1 || echo 0))
endif

# Base libraries (always required)
LDFLAGS = $(LDFLAGS_LIBFFI) $(LDFLAGS_OPENSSL) -lm -lpthread -lffi -ldl -lz -lcrypto

# Conditionally add libwebsockets
ifeq ($(HAS_LIBWEBSOCKETS),1)
LDFLAGS += $(LDFLAGS_LIBWEBSOCKETS) -lwebsockets
CFLAGS += -DHAVE_LIBWEBSOCKETS=1
endif

# Source files from src/ and src/parser/ and src/interpreter/ and src/interpreter/builtins/ and src/interpreter/io/ and src/interpreter/runtime/ and src/lsp/ and src/bundler/
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/parser/*.c) $(wildcard $(SRC_DIR)/interpreter/*.c) $(wildcard $(SRC_DIR)/interpreter/builtins/*.c) $(wildcard $(SRC_DIR)/interpreter/io/*.c) $(wildcard $(SRC_DIR)/interpreter/runtime/*.c) $(wildcard $(SRC_DIR)/lsp/*.c) $(wildcard $(SRC_DIR)/bundler/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = hemlock

all: $(BUILD_DIR) $(BUILD_DIR)/parser $(BUILD_DIR)/interpreter $(BUILD_DIR)/interpreter/builtins $(BUILD_DIR)/interpreter/io $(BUILD_DIR)/interpreter/runtime $(BUILD_DIR)/lsp $(BUILD_DIR)/bundler $(TARGET)

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

$(BUILD_DIR)/lsp:
	mkdir -p $(BUILD_DIR)/lsp

$(BUILD_DIR)/bundler:
	mkdir -p $(BUILD_DIR)/bundler

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) stdlib/c/*.so

run: $(TARGET)
	./$(TARGET)

test: $(TARGET) stdlib
	@bash tests/run_tests.sh

# ========== STDLIB C MODULES ==========

# Build stdlib C modules (lws_wrapper.so for HTTP/WebSocket)
.PHONY: stdlib
stdlib:
ifeq ($(HAS_LIBWEBSOCKETS),1)
	@echo "Building stdlib/c/lws_wrapper.so..."
ifeq ($(shell uname),Darwin)
	$(CC) -shared -fPIC -I$(BREW_LIBWEBSOCKETS)/include -I$(BREW_OPENSSL)/include -o stdlib/c/lws_wrapper.so stdlib/c/lws_wrapper.c $(LDFLAGS_LIBWEBSOCKETS) $(LDFLAGS_OPENSSL) -lwebsockets
else
	$(CC) -shared -fPIC -o stdlib/c/lws_wrapper.so stdlib/c/lws_wrapper.c -lwebsockets
endif
	@echo "✓ lws_wrapper.so built successfully"
else
	@echo "⊘ Skipping lws_wrapper.so (libwebsockets not installed)"
endif

# Clean stdlib builds
.PHONY: stdlib-clean
stdlib-clean:
	rm -f stdlib/c/*.so

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

.PHONY: all clean run test

# ========== COMPILER AND RUNTIME ==========

# Compiler source files (reuse lexer, parser, ast from interpreter)
# Modular codegen: core, expr, stmt, closure, program, module
COMPILER_SRCS = src/compiler/main.c $(wildcard src/compiler/codegen*.c) src/lexer.c src/ast.c $(wildcard src/parser/*.c)
COMPILER_OBJS = $(BUILD_DIR)/compiler/main.o $(BUILD_DIR)/compiler/codegen.o $(BUILD_DIR)/compiler/codegen_expr.o $(BUILD_DIR)/compiler/codegen_stmt.o $(BUILD_DIR)/compiler/codegen_closure.o $(BUILD_DIR)/compiler/codegen_program.o $(BUILD_DIR)/compiler/codegen_module.o $(BUILD_DIR)/lexer.o $(BUILD_DIR)/ast.o $(patsubst src/parser/%.c,$(BUILD_DIR)/parser/%.o,$(wildcard src/parser/*.c))
COMPILER_TARGET = hemlockc

# Runtime library
RUNTIME_DIR = runtime
RUNTIME_LIB = libhemlock_runtime.a

.PHONY: compiler runtime runtime-clean compiler-clean

# Build directory for compiler
$(BUILD_DIR)/compiler:
	mkdir -p $(BUILD_DIR)/compiler

# Compiler target
compiler: $(BUILD_DIR) $(BUILD_DIR)/compiler $(BUILD_DIR)/parser runtime $(COMPILER_TARGET)

$(COMPILER_TARGET): $(COMPILER_OBJS) $(RUNTIME_LIB)
	$(CC) $(COMPILER_OBJS) -o $(COMPILER_TARGET) -lm

$(BUILD_DIR)/compiler/%.o: src/compiler/%.c | $(BUILD_DIR)/compiler
	$(CC) $(CFLAGS) -c $< -o $@

# Build runtime library
runtime:
	@echo "Building Hemlock runtime library..."
	$(MAKE) -C $(RUNTIME_DIR) static
	cp $(RUNTIME_DIR)/build/$(RUNTIME_LIB) ./
	@echo "✓ Runtime library built: $(RUNTIME_LIB)"

runtime-clean:
	$(MAKE) -C $(RUNTIME_DIR) clean
	rm -f $(RUNTIME_LIB)

compiler-clean:
	rm -f $(COMPILER_TARGET) $(COMPILER_OBJS)

# Full clean including compiler and runtime
fullclean: clean compiler-clean runtime-clean

# Build everything (interpreter + compiler + runtime)
all-compiler: all compiler

# Run compiler test suite
.PHONY: test-compiler
test-compiler: compiler
	@bash tests/compiler/run_compiler_tests.sh

# Run parity test suite (tests that must pass on both interpreter and compiler)
.PHONY: parity
parity: $(TARGET) compiler
	@bash tests/parity/run_parity_tests.sh

# Run full parity test (all interpreter tests through compiler)
.PHONY: parity-full
parity-full: $(TARGET) compiler
	@bash tests/run_full_parity.sh

# Run bundler test suite
.PHONY: test-bundler
test-bundler: $(TARGET)
	@bash tests/bundler/run_bundler_tests.sh

# Run all test suites
.PHONY: test-all
test-all: test test-compiler parity test-bundler

# ========== INSTALLATION ==========

# Installation directories (can be overridden)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/hemlock
DESTDIR ?=

.PHONY: install uninstall

install: $(TARGET)
	@echo "Installing Hemlock to $(DESTDIR)$(PREFIX)..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Installing stdlib to $(DESTDIR)$(LIBDIR)..."
	install -d $(DESTDIR)$(LIBDIR)/stdlib
	cp -r stdlib/* $(DESTDIR)$(LIBDIR)/stdlib/
	@echo ""
	@echo "✓ Hemlock installed successfully"
	@echo "  Binary: $(DESTDIR)$(BINDIR)/$(TARGET)"
	@echo "  Stdlib: $(DESTDIR)$(LIBDIR)/stdlib/"

uninstall:
	@echo "Uninstalling Hemlock from $(DESTDIR)$(PREFIX)..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(LIBDIR)
	@echo "✓ Hemlock uninstalled"

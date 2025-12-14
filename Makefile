CC = gcc
# Use _DARWIN_C_SOURCE on macOS for BSD types, _POSIX_C_SOURCE on Linux
ifeq ($(shell uname),Darwin)
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -D_DARWIN_C_SOURCE -Iinclude -Isrc -Isrc/frontend -Isrc/backends
else
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc -Isrc/frontend -Isrc/backends
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

# ========== SOURCE FILES (New Structure) ==========
# Frontend: shared lexer, parser, AST
FRONTEND_SRCS = $(wildcard $(SRC_DIR)/frontend/*.c) $(wildcard $(SRC_DIR)/frontend/parser/*.c)

# Frontend files for compiler (exclude module.c which has interpreter-specific code)
FRONTEND_COMPILER_SRCS = $(SRC_DIR)/frontend/ast.c \
                         $(SRC_DIR)/frontend/ast_serialize.c \
                         $(SRC_DIR)/frontend/lexer.c \
                         $(wildcard $(SRC_DIR)/frontend/parser/*.c)

# Interpreter backend
INTERP_SRCS = $(wildcard $(SRC_DIR)/backends/interpreter/*.c) \
              $(wildcard $(SRC_DIR)/backends/interpreter/builtins/*.c) \
              $(wildcard $(SRC_DIR)/backends/interpreter/io/*.c) \
              $(wildcard $(SRC_DIR)/backends/interpreter/runtime/*.c)

# Other components
OTHER_SRCS = $(wildcard $(SRC_DIR)/lsp/*.c) $(wildcard $(SRC_DIR)/bundler/*.c)

# All interpreter sources
SRCS = $(FRONTEND_SRCS) $(INTERP_SRCS) $(OTHER_SRCS)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = hemlock

# Build directories
BUILD_DIRS = $(BUILD_DIR) \
             $(BUILD_DIR)/frontend \
             $(BUILD_DIR)/frontend/parser \
             $(BUILD_DIR)/backends/interpreter \
             $(BUILD_DIR)/backends/interpreter/builtins \
             $(BUILD_DIR)/backends/interpreter/io \
             $(BUILD_DIR)/backends/interpreter/runtime \
             $(BUILD_DIR)/backends/compiler \
             $(BUILD_DIR)/lsp \
             $(BUILD_DIR)/bundler

all: $(BUILD_DIRS) $(TARGET)

$(BUILD_DIRS):
	mkdir -p $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Special rule for ffi.c - compile with -O0 to work around an optimizer bug
# that causes infinite loops when FFI functions are called from Hemlock code
# with while loops in helper functions. The root cause appears to be undefined
# behavior exposed only at -O1/-O2/-O3 optimization levels.
$(BUILD_DIR)/backends/interpreter/ffi.o: $(SRC_DIR)/backends/interpreter/ffi.c | $(BUILD_DIRS)
	$(CC) $(subst -O3,-O0,$(CFLAGS)) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIRS)
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

# Compiler source files (reuse frontend from interpreter, but not module.c)
# Modular codegen: core, expr, stmt, closure, program, module
COMPILER_SRCS = $(SRC_DIR)/backends/compiler/main.c \
                $(wildcard $(SRC_DIR)/backends/compiler/codegen*.c) \
                $(FRONTEND_COMPILER_SRCS)

COMPILER_OBJS = $(BUILD_DIR)/backends/compiler/main.o \
                $(BUILD_DIR)/backends/compiler/codegen.o \
                $(BUILD_DIR)/backends/compiler/codegen_expr.o \
                $(BUILD_DIR)/backends/compiler/codegen_stmt.o \
                $(BUILD_DIR)/backends/compiler/codegen_closure.o \
                $(BUILD_DIR)/backends/compiler/codegen_program.o \
                $(BUILD_DIR)/backends/compiler/codegen_module.o \
                $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(FRONTEND_COMPILER_SRCS))

COMPILER_TARGET = hemlockc

# Runtime library
RUNTIME_DIR = runtime
RUNTIME_LIB = libhemlock_runtime.a

.PHONY: compiler runtime runtime-clean compiler-clean

# Compiler target
compiler: $(BUILD_DIRS) runtime $(COMPILER_TARGET)

$(COMPILER_TARGET): $(COMPILER_OBJS) $(RUNTIME_LIB)
	$(CC) $(COMPILER_OBJS) -o $(COMPILER_TARGET) -lm

# Compiler objects get LIBDIR defined for runtime auto-detection
$(BUILD_DIR)/backends/compiler/%.o: $(SRC_DIR)/backends/compiler/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -DHEMLOCK_LIBDIR='"$(LIBDIR)"' -c $< -o $@

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

# Full clean including compiler, runtime, and release
fullclean: clean compiler-clean runtime-clean release-clean

# Build everything (interpreter + compiler + runtime)
all-compiler: all compiler

# Run compiler test suite
.PHONY: test-compiler
test-compiler: compiler
	@bash tests/compiler/run_compiler_tests.sh

# Check that interpreter tests compile (does not check output parity)
.PHONY: compile-check
compile-check: compiler
	@bash tests/run_compile_check.sh

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

# ========== RELEASE BUILD ==========

# Release flags: optimize for performance, no debug symbols
ifeq ($(shell uname),Darwin)
    RELEASE_CFLAGS = -Wall -Wextra -std=c11 -O3 -D_DARWIN_C_SOURCE -Iinclude -Isrc -Isrc/frontend -Isrc/backends
else
    RELEASE_CFLAGS = -Wall -Wextra -std=c11 -O3 -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc -Isrc/frontend -Isrc/backends
endif

# Add the same conditional flags as regular build
ifeq ($(shell uname),Darwin)
    ifneq ($(BREW_LIBFFI),)
        RELEASE_CFLAGS += -I$(BREW_LIBFFI)/include
    endif
    ifneq ($(BREW_OPENSSL),)
        RELEASE_CFLAGS += -I$(BREW_OPENSSL)/include
    endif
    ifeq ($(HAS_LIBWEBSOCKETS),1)
        RELEASE_CFLAGS += -I$(BREW_LIBWEBSOCKETS)/include
    endif
else
    ifneq ($(LIBFFI_CFLAGS),)
        RELEASE_CFLAGS += $(LIBFFI_CFLAGS)
    endif
endif
ifeq ($(HAS_LIBWEBSOCKETS),1)
    RELEASE_CFLAGS += -DHAVE_LIBWEBSOCKETS=1
endif

# Release build directory
RELEASE_BUILD_DIR = build-release
RELEASE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(RELEASE_BUILD_DIR)/%.o,$(SRCS))

.PHONY: release release-clean

# Release build directories
RELEASE_BUILD_DIRS = $(RELEASE_BUILD_DIR) \
                     $(RELEASE_BUILD_DIR)/frontend \
                     $(RELEASE_BUILD_DIR)/frontend/parser \
                     $(RELEASE_BUILD_DIR)/backends/interpreter \
                     $(RELEASE_BUILD_DIR)/backends/interpreter/builtins \
                     $(RELEASE_BUILD_DIR)/backends/interpreter/io \
                     $(RELEASE_BUILD_DIR)/backends/interpreter/runtime \
                     $(RELEASE_BUILD_DIR)/lsp \
                     $(RELEASE_BUILD_DIR)/bundler

# Build optimized, stripped binary for distribution
release: $(RELEASE_BUILD_DIRS) $(RELEASE_BUILD_DIR)/hemlock
	@echo ""
	@echo "✓ Release build complete: $(RELEASE_BUILD_DIR)/hemlock"
	@ls -lh $(RELEASE_BUILD_DIR)/hemlock

$(RELEASE_BUILD_DIRS):
	mkdir -p $@

$(RELEASE_BUILD_DIR)/hemlock: $(RELEASE_OBJS)
	$(CC) $(RELEASE_OBJS) -o $@ $(LDFLAGS)
	strip $@

# Special rule for release build - ffi.c needs O0 to avoid optimizer bugs
$(RELEASE_BUILD_DIR)/backends/interpreter/ffi.o: $(SRC_DIR)/backends/interpreter/ffi.c
	@mkdir -p $(dir $@)
	$(CC) $(subst -O3,-O0,$(RELEASE_CFLAGS)) -c $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RELEASE_CFLAGS) -c $< -o $@

release-clean:
	rm -rf $(RELEASE_BUILD_DIR)

# ========== INSTALLATION ==========

# Installation directories (can be overridden)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/hemlock
DESTDIR ?=

.PHONY: install install-compiler uninstall

install: $(TARGET)
	@echo "Installing Hemlock to $(DESTDIR)$(PREFIX)..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Installing stdlib to $(DESTDIR)$(LIBDIR)..."
	install -d $(DESTDIR)$(LIBDIR)/stdlib
	cp -r stdlib/* $(DESTDIR)$(LIBDIR)/stdlib/
	@# Install compiler if it was built
	@if [ -f $(COMPILER_TARGET) ]; then \
		echo "Installing compiler to $(DESTDIR)$(BINDIR)..."; \
		install -m 755 $(COMPILER_TARGET) $(DESTDIR)$(BINDIR)/$(COMPILER_TARGET); \
		echo "  Compiler: $(DESTDIR)$(BINDIR)/$(COMPILER_TARGET)"; \
	fi
	@# Install runtime library if it was built
	@if [ -f $(RUNTIME_LIB) ]; then \
		echo "Installing runtime library to $(DESTDIR)$(LIBDIR)..."; \
		install -d $(DESTDIR)$(LIBDIR); \
		install -m 644 $(RUNTIME_LIB) $(DESTDIR)$(LIBDIR)/$(RUNTIME_LIB); \
		echo "  Runtime: $(DESTDIR)$(LIBDIR)/$(RUNTIME_LIB)"; \
	fi
	@# Install runtime headers if available
	@if [ -d $(RUNTIME_DIR)/include ]; then \
		echo "Installing runtime headers to $(DESTDIR)$(LIBDIR)/include..."; \
		install -d $(DESTDIR)$(LIBDIR)/include; \
		cp -r $(RUNTIME_DIR)/include/* $(DESTDIR)$(LIBDIR)/include/; \
		echo "  Headers: $(DESTDIR)$(LIBDIR)/include/"; \
	fi
	@echo ""
	@echo "✓ Hemlock installed successfully"
	@echo "  Binary: $(DESTDIR)$(BINDIR)/$(TARGET)"
	@echo "  Stdlib: $(DESTDIR)$(LIBDIR)/stdlib/"

# Install compiler and runtime (builds them first if needed)
install-compiler: compiler
	@echo "Installing Hemlock compiler to $(DESTDIR)$(PREFIX)..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(COMPILER_TARGET) $(DESTDIR)$(BINDIR)/$(COMPILER_TARGET)
	@echo "Installing runtime library to $(DESTDIR)$(LIBDIR)..."
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(RUNTIME_LIB) $(DESTDIR)$(LIBDIR)/$(RUNTIME_LIB)
	@# Install runtime headers if available
	@if [ -d $(RUNTIME_DIR)/include ]; then \
		echo "Installing runtime headers to $(DESTDIR)$(LIBDIR)/include..."; \
		install -d $(DESTDIR)$(LIBDIR)/include; \
		cp -r $(RUNTIME_DIR)/include/* $(DESTDIR)$(LIBDIR)/include/; \
	fi
	@echo ""
	@echo "✓ Hemlock compiler installed successfully"
	@echo "  Compiler: $(DESTDIR)$(BINDIR)/$(COMPILER_TARGET)"
	@echo "  Runtime: $(DESTDIR)$(LIBDIR)/$(RUNTIME_LIB)"

uninstall:
	@echo "Uninstalling Hemlock from $(DESTDIR)$(PREFIX)..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/$(COMPILER_TARGET)
	rm -rf $(DESTDIR)$(LIBDIR)
	@echo "✓ Hemlock uninstalled"

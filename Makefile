CC = gcc

# ---- Windows (MinGW-w64) detection ----
# Set when building natively under MSYS2/MinGW (uname reports MINGW*/MSYS*)
# or when cross-compiling with a *-mingw32 toolchain, e.g.:
#   make mingw                                (convenience target, see below)
#   make CC=x86_64-w64-mingw32-gcc-posix all  (explicit toolchain)
# Requires the POSIX-threads flavor of MinGW-w64 (winpthreads).
ifneq (,$(findstring mingw,$(CC))$(findstring MINGW,$(shell uname))$(findstring MSYS,$(shell uname)))
    HEMLOCK_WINDOWS = 1
    EXE = .exe
endif

# -MMD -MP: emit a .d file alongside each .o that lists every (non-system)
# header the .c file pulled in. Without these, `make` only rebuilds .o files
# when their .c source changes — touching a header silently produces a
# binary linked from objects compiled against different versions of the same
# header, which surfaces as cryptic runtime breakage (e.g. parser objects
# expecting one AST struct layout, builtin objects expecting another).
# Use _DARWIN_C_SOURCE on macOS for BSD types, _POSIX_C_SOURCE on Linux
ifeq ($(HEMLOCK_WINDOWS),1)
    # FFI on Windows: enabled when the MinGW toolchain can link libffi
    # (MSYS2: pacman -S mingw-w64-ucrt-x86_64-libffi; cross builds: build
    # libffi from source with --host=x86_64-w64-mingw32, see
    # docs/advanced/windows.md). Without it, FFI builtins throw at runtime.
    # --print-file-name echoes the bare name back when the lib is missing
    MINGW_FFI_PATH := $(shell $(CC) --print-file-name=libffi.a 2>/dev/null)
    ifneq ($(MINGW_FFI_PATH),libffi.a)
        HAS_MINGW_FFI = 1
    else
        HAS_MINGW_FFI = 0
    endif
    ifeq ($(HAS_MINGW_FFI),1)
        WIN_FFI_CFLAGS =
        WIN_FFI_LIB = -lffi
    else
        WIN_FFI_CFLAGS = -DHEMLOCK_NO_FFI
        WIN_FFI_LIB =
    endif
    # __USE_MINGW_ANSI_STDIO: C99 printf (%zu, %lld) instead of msvcrt's.
    # HEMLOCK_NO_OPENSSL: OpenSSL is typically not available for MinGW
    # cross builds; the hash builtins (sha1/sha256/sha512/md5) fall back
    # to Windows CNG (bcrypt.dll) and only ECDSA throws at runtime (see
    # wasm_interp_shim.c). Override with EXTRA_CFLAGS/EXTRA_LDFLAGS on
    # MSYS2 where OpenSSL is installable via pacman.
    # Implicit declarations are hard errors so older cross toolchains
    # (GCC <= 13) catch what GCC 14+ on native Windows rejects by default.
    # src/shared/regex_win32 supplies <regex.h> (bundled musl/TRE engine —
    # MinGW has none), so the regex builtins use their POSIX code path.
    CFLAGS = -Wall -Wextra -std=c11 -fwrapv -O3 -g -MMD -MP -Werror=implicit-function-declaration -Werror=int-conversion -D_WIN32_WINNT=0x0601 -D__USE_MINGW_ANSI_STDIO=1 $(WIN_FFI_CFLAGS) -DHEMLOCK_NO_OPENSSL -Iinclude -Isrc -Isrc/frontend -Isrc/backends -Isrc/shared -Isrc/shared/regex_win32 $(EXTRA_CFLAGS)
else ifeq ($(shell uname),Darwin)
    CFLAGS = -Wall -Wextra -std=c11 -fwrapv -O3 -g -MMD -MP -D_DARWIN_C_SOURCE -Iinclude -Isrc -Isrc/frontend -Isrc/backends -Isrc/shared $(EXTRA_CFLAGS)
else
    CFLAGS = -Wall -Wextra -std=c11 -fwrapv -O3 -g -MMD -MP -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude -Isrc -Isrc/frontend -Isrc/backends -Isrc/shared $(EXTRA_CFLAGS)
endif
SRC_DIR = src
BUILD_DIR = build
# Cross-compiling for Windows from a POSIX host: keep PE objects out of the
# native build tree
ifeq ($(HEMLOCK_WINDOWS),1)
ifeq (,$(findstring MINGW,$(shell uname))$(findstring MSYS,$(shell uname)))
    BUILD_DIR = build-mingw
endif
endif

# Detect libffi, OpenSSL, and libwebsockets (Homebrew on macOS puts them in non-standard locations)
ifeq ($(HEMLOCK_WINDOWS),1)
    # No pkg-config probing for cross builds; FFI/OpenSSL are disabled above
    HAS_LIBWEBSOCKETS := 0
else ifeq ($(shell uname),Darwin)
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
ifeq ($(HEMLOCK_WINDOWS),1)
# winpthreads for threading, ws2_32 for sockets, bcrypt for CNG hashing;
# no -ldl/-lcrypto. winpthreads, zlib, and (when present) libffi are
# linked statically so hemlock.exe is self-contained (ws2_32/bcrypt are
# system DLLs present on every Windows install).
LDFLAGS = -static-libgcc -Wl,-Bstatic -lpthread -lz $(WIN_FFI_LIB) -Wl,-Bdynamic -lm -lws2_32 -lbcrypt $(EXTRA_LDFLAGS)
else
LDFLAGS = $(LDFLAGS_LIBFFI) $(LDFLAGS_OPENSSL) -lm -lpthread -lffi -ldl -lz -lcrypto $(EXTRA_LDFLAGS)
endif

# Conditionally add libwebsockets
ifeq ($(HAS_LIBWEBSOCKETS),1)
LDFLAGS += $(LDFLAGS_LIBWEBSOCKETS) -lwebsockets
CFLAGS += -DHAVE_LIBWEBSOCKETS=1
endif

# ========== SOURCE FILES (New Structure) ==========
# Shared: pure functions used by both interpreter and runtime
SHARED_SRCS = $(wildcard $(SRC_DIR)/shared/*.c)

# Frontend: shared lexer, parser, AST, and modules
MODULES_SRCS = $(wildcard $(SRC_DIR)/modules/*.c)
FRONTEND_SRCS = $(wildcard $(SRC_DIR)/frontend/lexer/*.c) \
                $(wildcard $(SRC_DIR)/frontend/parser/*.c) \
                $(wildcard $(SRC_DIR)/frontend/resolver/*.c) \
                $(wildcard $(SRC_DIR)/frontend/optimizer/*.c)

# Frontend files for compiler (exclude module.c which has interpreter-specific code)
FRONTEND_COMPILER_SRCS = $(SRC_DIR)/frontend/lexer/lexer.c \
                         $(SRC_DIR)/frontend/optimizer/optimizer.c \
                         $(SRC_DIR)/frontend/resolver/resolver.c \
                         $(filter-out $(SRC_DIR)/frontend/parser/module.c, $(wildcard $(SRC_DIR)/frontend/parser/*.c)) \
                         $(MODULES_SRCS)

# Interpreter backend
INTERP_SRCS = $(wildcard $(SRC_DIR)/backends/interpreter/*.c) \
              $(wildcard $(SRC_DIR)/backends/interpreter/builtins/*.c) \
              $(wildcard $(SRC_DIR)/backends/interpreter/io/*.c) \
              $(wildcard $(SRC_DIR)/backends/interpreter/runtime/*.c) \
              $(wildcard $(SRC_DIR)/backends/interpreter/profiler/*.c)

# Tooling components
TOOL_SRCS = $(wildcard $(SRC_DIR)/tools/lsp/*.c) \
            $(wildcard $(SRC_DIR)/tools/bundler/*.c) \
            $(wildcard $(SRC_DIR)/tools/formatter/*.c)

# Shared compiler utilities (used by LSP)
TYPECHECK_SRCS = $(wildcard $(SRC_DIR)/backends/compiler/type_*.c)

# Borrow/ownership checker — shared so the LSP can surface its diagnostics
BORROWCHECK_SRCS = $(SRC_DIR)/backends/compiler/borrow_check.c

# Static lint / diagnostics pass — shared so `hemlock check` can run it
LINTCHECK_SRCS = $(SRC_DIR)/backends/compiler/lint.c

COMMON_SRCS = $(FRONTEND_SRCS) $(MODULES_SRCS) $(TYPECHECK_SRCS) $(BORROWCHECK_SRCS) $(LINTCHECK_SRCS) $(SHARED_SRCS)
SRCS = $(COMMON_SRCS) $(TOOL_SRCS) $(INTERP_SRCS)

COMMON_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
TOOL_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(TOOL_SRCS))
INTERP_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(INTERP_SRCS))

LIBCOMMON = $(BUILD_DIR)/libcommon.a
LIBTOOLS = $(BUILD_DIR)/libtools.a

TARGET = hemlock$(EXE)

# Build directories
BUILD_DIRS = $(BUILD_DIR) \
             $(BUILD_DIR)/shared \
             $(BUILD_DIR)/frontend \
             $(BUILD_DIR)/frontend/lexer \
             $(BUILD_DIR)/frontend/parser \
             $(BUILD_DIR)/frontend/resolver \
             $(BUILD_DIR)/frontend/optimizer \
             $(BUILD_DIR)/backends/interpreter \
             $(BUILD_DIR)/backends/interpreter/builtins \
             $(BUILD_DIR)/backends/interpreter/io \
             $(BUILD_DIR)/backends/interpreter/runtime \
             $(BUILD_DIR)/backends/interpreter/profiler \
             $(BUILD_DIR)/backends/compiler \
             $(BUILD_DIR)/tools \
             $(BUILD_DIR)/tools/lsp \
             $(BUILD_DIR)/tools/bundler \
             $(BUILD_DIR)/tools/formatter \
             $(BUILD_DIR)/modules

all: $(BUILD_DIRS) $(TARGET) compiler

$(BUILD_DIRS):
	mkdir -p $@

$(LIBCOMMON): $(COMMON_OBJS)
	ar rcs $@ $^

$(LIBTOOLS): $(TOOL_OBJS)
	ar rcs $@ $^

$(TARGET): $(INTERP_OBJS) $(LIBTOOLS) $(LIBCOMMON)
	$(CC) $(INTERP_OBJS) $(LIBTOOLS) $(LIBCOMMON) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIRS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) build-mingw $(TARGET) hemlock.exe hemlockc.exe stdlib/c/*.so
	$(MAKE) -C runtime clean
	rm -f $(RUNTIME_LIB) $(RUNTIME_SHARED) libhemlock_runtime.dll

run: $(TARGET)
	./$(TARGET)

test: $(TARGET) stdlib
	@bash tests/run_tests.sh

# Test the persistent WASM context API (native, no Emscripten needed)
TEST_WASM_CTX_SRC = tests/wasm/test_persistent_context.c
TEST_WASM_CTX_BIN = $(BUILD_DIR)/test_persistent_context
TEST_WASM_CTX_OBJS = $(filter-out $(BUILD_DIR)/backends/interpreter/main.o \
                                   $(BUILD_DIR)/backends/interpreter/wasm_interp_main.o \
                                   $(BUILD_DIR)/backends/interpreter/wasm_interp_shim.o, \
                                   $(INTERP_OBJS))

.PHONY: test-wasm-context
test-wasm-context: $(INTERP_OBJS) $(LIBTOOLS) $(LIBCOMMON)
	@echo "Building persistent context API test..."
	@$(CC) $(CFLAGS) $(TEST_WASM_CTX_SRC) $(TEST_WASM_CTX_OBJS) $(LIBTOOLS) $(LIBCOMMON) \
		-o $(TEST_WASM_CTX_BIN) $(LDFLAGS)
	@echo "Running persistent context API test..."
	@timeout 30 $(TEST_WASM_CTX_BIN)

test-formatter: $(TARGET)
	@bash tests/formatter/run_tests.sh

.PHONY: test-cli
test-cli: $(TARGET)
	@bash tests/cli/run_cli_tests.sh

# Run `hemlock check` static-checker tests
.PHONY: test-check
test-check: $(TARGET)
	@bash tests/check/run_check_tests.sh

# Regenerate the stdlib API index (docs/reference/stdlib-api-index.md)
.PHONY: stdlib-api
stdlib-api:
	@python3 tests/gen_stdlib_api.py

# Documentation audit: link/doc pairing checks + stdlib API index freshness
.PHONY: docs-check
docs-check:
	@python3 tests/check_docs.py
	@python3 tests/gen_stdlib_api.py --check

# ========== STDLIB C MODULES ==========

# Build stdlib C modules (lws_wrapper.so for HTTP/WebSocket, ffi_struct_test for tests)
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
	@echo "Building stdlib/c/libffi_struct_test..."
ifeq ($(shell uname),Darwin)
	$(CC) -shared -fPIC -o stdlib/c/libffi_struct_test.dylib stdlib/c/ffi_struct_test.c
else
	$(CC) -shared -fPIC -o stdlib/c/libffi_struct_test.so stdlib/c/ffi_struct_test.c
endif
	@echo "✓ libffi_struct_test built successfully"

# Clean stdlib builds
.PHONY: stdlib-clean
stdlib-clean:
	rm -f stdlib/c/*.so stdlib/c/*.dylib

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

# ========== ADDRESS SANITIZER (ASAN) MEMORY LEAK DETECTION ==========

# Build with AddressSanitizer for leak detection
# ASAN catches: memory leaks, use-after-free, buffer overflows, double-free
ASAN_CFLAGS = -fsanitize=address -fno-omit-frame-pointer -g -O1
ASAN_LDFLAGS = -fsanitize=address

# Build interpreter with ASAN (not compiler - it has separate build rules)
.PHONY: asan
asan: clean
	@echo "Building hemlock interpreter with AddressSanitizer..."
	$(MAKE) $(TARGET) EXTRA_CFLAGS="$(ASAN_CFLAGS)" EXTRA_LDFLAGS="$(ASAN_LDFLAGS)"
	@echo ""
	@echo "✓ ASAN build complete. Run tests with: make asan-test"
	@echo "  Or run directly: ASAN_OPTIONS=detect_leaks=1 ./hemlock script.hml"

# Run test suite with ASAN
.PHONY: asan-test
asan-test: asan
	@echo "Running test suite with AddressSanitizer (leak detection enabled)..."
	@echo "This will report any memory leaks in the runtime."
	@echo ""
	ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:print_stats=1" timeout 120 $(MAKE) test || true
	@echo ""
	@echo "✓ ASAN test run complete. Check output above for leak reports."

# Run specific file with ASAN
.PHONY: asan-file
asan-file: asan
ifndef FILE
	@echo "Usage: make asan-file FILE=path/to/test.hml"
	@exit 1
endif
	@echo "Running $(FILE) with AddressSanitizer..."
	ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" ./$(TARGET) $(FILE)

# Quick ASAN check - build and run basic test
.PHONY: asan-quick
asan-quick: asan
	@echo "Quick ASAN check..."
	@echo 'let x = alloc(100); free(x); print("ok");' > /tmp/asan_test.hml
	ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" ./$(TARGET) /tmp/asan_test.hml
	@rm -f /tmp/asan_test.hml
	@echo ""
	@echo "✓ Quick ASAN check passed - no leaks detected"

# Comprehensive leak verification - runs full test suite
# This provides memory safety guarantees for the runtime
.PHONY: leak-check
leak-check: asan
	@echo ""
	@echo "Running comprehensive memory leak verification..."
	@./tests/leak_check.sh

# Quick leak check - core tests only
.PHONY: leak-check-quick
leak-check-quick: asan
	@echo ""
	@echo "Running quick memory leak verification..."
	@./tests/leak_check.sh --quick

# Verbose leak check - shows all test output
.PHONY: leak-check-verbose
leak-check-verbose: asan
	@echo ""
	@echo "Running verbose memory leak verification..."
	@./tests/leak_check.sh --verbose

# Run the comprehensive stress test only
.PHONY: leak-stress
leak-stress: asan
	@echo "Running comprehensive leak stress test..."
	ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:print_stats=1" ./$(TARGET) tests/memory/comprehensive_leak_test.hml

# Run leak regression test suite (all memory leak fixes)
.PHONY: leak-regression
leak-regression: asan
	@echo ""
	@./tests/memory/regression/run_leak_tests.sh

# Quick leak regression test (skip comprehensive test)
.PHONY: leak-regression-quick
leak-regression-quick: asan
	@echo ""
	@./tests/memory/regression/run_leak_tests.sh --quick

# ========== CONCURRENCY / LIFETIME STRESS HARNESS ==========
#
# tests/stress/*.hml are compiled as NATIVE binaries (these bugs —
# refcount exhaustion, concurrent object/string heap corruption —
# only exist in the compiled runtime, not the interpreter) and run
# under an optional sanitizer. Each .hml is a regression test for a
# concrete crash diagnosed from a long-running Hemlock service.
#
#   make stress        plain build, crash/exit check (fast)
#   make stress-asan    AddressSanitizer: use-after-free / double-free
#   make stress-tsan    ThreadSanitizer: data races
#   make stress-all     all three
#
# Requires the compiler (hemlockc + stdlib). The harness builds its
# own sanitizer runtime into a temp dir; it never touches the normal
# ./libhemlock_runtime.a.

.PHONY: stress
stress: compiler stdlib
	@bash tests/stress/run_stress.sh none

.PHONY: stress-asan
stress-asan: compiler stdlib
	@bash tests/stress/run_stress.sh asan

.PHONY: stress-tsan
stress-tsan: compiler stdlib
	@bash tests/stress/run_stress.sh tsan

# Leak regression guard. Runs only the dedicated *_leak.hml tests
# under LeakSanitizer (the broader concurrency tests have separate
# pre-existing leaks under audit and would mask a real regression).
.PHONY: stress-lsan
stress-lsan: compiler stdlib
	@bash tests/stress/run_stress.sh lsan

.PHONY: stress-all
stress-all: stress stress-asan stress-lsan stress-tsan

# ========== CLANG STATIC ANALYSIS ==========

# Check if clang-tidy and scan-build are installed
CLANG_TIDY := $(shell command -v clang-tidy 2> /dev/null)
SCAN_BUILD := $(shell command -v scan-build 2> /dev/null)

# Source files to analyze (exclude runtime - it has its own build system)
ANALYZE_SRCS = $(FRONTEND_SRCS) $(INTERP_SRCS) $(OTHER_SRCS) \
               $(wildcard $(SRC_DIR)/backends/compiler/*.c)

# clang-tidy: Run clang-tidy linter on all source files
.PHONY: clang-tidy
clang-tidy:
ifndef CLANG_TIDY
	@echo "⚠ clang-tidy not found. Install with: sudo apt-get install clang-tidy"
	@exit 1
endif
	@echo "Running clang-tidy on Hemlock source files..."
	@echo "Configuration: .clang-tidy"
	@echo ""
	@failed=0; \
	for file in $(ANALYZE_SRCS); do \
		if [ -f "$$file" ]; then \
			echo "Analyzing: $$file"; \
			clang-tidy $$file -- $(CFLAGS) 2>/dev/null || failed=1; \
		fi; \
	done; \
	if [ $$failed -eq 0 ]; then \
		echo ""; \
		echo "✓ clang-tidy analysis complete - no issues found"; \
	else \
		echo ""; \
		echo "⚠ clang-tidy found issues (see above)"; \
	fi

# clang-tidy-fix: Run clang-tidy with automatic fixes
.PHONY: clang-tidy-fix
clang-tidy-fix:
ifndef CLANG_TIDY
	@echo "⚠ clang-tidy not found. Install with: sudo apt-get install clang-tidy"
	@exit 1
endif
	@echo "Running clang-tidy with automatic fixes..."
	@echo "WARNING: This will modify source files in place"
	@echo ""
	@for file in $(ANALYZE_SRCS); do \
		if [ -f "$$file" ]; then \
			echo "Fixing: $$file"; \
			clang-tidy -fix $$file -- $(CFLAGS) 2>/dev/null || true; \
		fi; \
	done
	@echo ""
	@echo "✓ clang-tidy fixes applied"

# scan-build: Run clang static analyzer
.PHONY: scan-build
scan-build: clean
ifndef SCAN_BUILD
	@echo "⚠ scan-build not found. Install with: sudo apt-get install clang-tools"
	@exit 1
endif
	@echo "Running clang static analyzer via scan-build..."
	@echo "This performs a full build with analysis enabled"
	@echo ""
	scan-build --status-bugs -o scan-build-reports make all
	@echo ""
	@echo "✓ scan-build analysis complete"
	@echo "  Reports (if any): scan-build-reports/"

# scan-build-view: Open scan-build HTML reports
.PHONY: scan-build-view
scan-build-view:
	@if [ -d scan-build-reports ]; then \
		latest=$$(ls -td scan-build-reports/*/ 2>/dev/null | head -1); \
		if [ -n "$$latest" ]; then \
			echo "Opening: $$latest/index.html"; \
			xdg-open "$$latest/index.html" 2>/dev/null || open "$$latest/index.html" 2>/dev/null || echo "Open $$latest/index.html in your browser"; \
		else \
			echo "No reports found in scan-build-reports/"; \
		fi; \
	else \
		echo "No scan-build-reports directory. Run 'make scan-build' first."; \
	fi

# analyze: Run all static analysis tools
.PHONY: analyze
analyze: clang-tidy scan-build
	@echo ""
	@echo "✓ All static analysis complete"

# analyze-quick: Run only clang-tidy (faster, no rebuild required)
.PHONY: analyze-quick
analyze-quick: clang-tidy

# Clean analysis artifacts
.PHONY: analyze-clean
analyze-clean:
	@echo "Removing static analysis reports..."
	@rm -rf scan-build-reports
	@echo "Done."

# ========== COMPILER AND RUNTIME ==========

# Compiler source files (reuse frontend from interpreter, but not module.c)
# Modular codegen: core, expr, stmt, closure, program, module
COMPILER_SRCS = $(SRC_DIR)/backends/compiler/main.c \
                $(wildcard $(SRC_DIR)/backends/compiler/codegen*.c) \
                $(BORROWCHECK_SRCS) \
                $(LINTCHECK_SRCS) \
                $(TYPECHECK_SRCS) \
                $(SHARED_SRCS) \
                $(FRONTEND_COMPILER_SRCS)

COMPILER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(COMPILER_SRCS))

COMPILER_TARGET = hemlockc$(EXE)

# Runtime library
RUNTIME_DIR = runtime
# Mirrors the BUILD_DIR override above: cross builds keep their objects in
# runtime/build-mingw (runtime/Makefile applies the same rule)
ifeq ($(BUILD_DIR),build-mingw)
    RUNTIME_BUILD = $(RUNTIME_DIR)/build-mingw
else
    RUNTIME_BUILD = $(RUNTIME_DIR)/build
endif
RUNTIME_LIB = libhemlock_runtime.a
ifeq ($(HEMLOCK_WINDOWS),1)
    RUNTIME_SHARED = libhemlock_runtime.dll
else ifeq ($(shell uname),Darwin)
    RUNTIME_SHARED = libhemlock_runtime.dylib
else
    RUNTIME_SHARED = libhemlock_runtime.so
endif

.PHONY: compiler runtime runtime-clean compiler-clean

# Compiler target
compiler: $(BUILD_DIRS) runtime $(COMPILER_TARGET)

# The driver itself needs no extra libs; on Windows platform_win32.o pulls
# in winsock (WSAStartup) and CNG (BCrypt*) so ws2_32/bcrypt must be linked
ifeq ($(HEMLOCK_WINDOWS),1)
# -static keeps winpthreads/libgcc out of the DLL deps (the POSIX-flavor
# toolchain appends a dynamic -lpthread on its own otherwise); system DLLs
# (msvcrt, ws2_32, bcrypt) stay dynamic regardless
COMPILER_LDFLAGS = -static -lm -lws2_32 -lbcrypt
else
COMPILER_LDFLAGS = -lm
endif

# Cross builds must not overwrite the native runtime lib in the repo root
# (hemlockc auto-detects ./libhemlock_runtime.a); they keep theirs in
# runtime/build-mingw and pass it via `hemlockc --runtime`
ifeq ($(BUILD_DIR),build-mingw)
    RUNTIME_DEP = $(RUNTIME_BUILD)/$(RUNTIME_LIB)
else
    RUNTIME_DEP = $(RUNTIME_LIB)
endif

$(COMPILER_TARGET): $(COMPILER_OBJS) $(RUNTIME_DEP)
	$(CC) $(COMPILER_OBJS) -o $(COMPILER_TARGET) $(COMPILER_LDFLAGS)

# Compiler objects get LIBDIR defined for runtime auto-detection
$(BUILD_DIR)/backends/compiler/%.o: $(SRC_DIR)/backends/compiler/%.c | $(BUILD_DIRS)
	$(CC) $(CFLAGS) -DHEMLOCK_LIBDIR='"$(LIBDIR)"' -c $< -o $@

# Build runtime library (phony target for explicit invocation)
runtime:
	@echo "Building Hemlock runtime library..."
	$(MAKE) -C $(RUNTIME_DIR) static shared
ifeq ($(BUILD_DIR),build-mingw)
	@echo "✓ Windows runtime library built: $(RUNTIME_BUILD)/$(RUNTIME_LIB) + $(RUNTIME_BUILD)/$(RUNTIME_SHARED)"
else
	cp $(RUNTIME_BUILD)/$(RUNTIME_LIB) ./
	cp $(RUNTIME_BUILD)/$(RUNTIME_SHARED) ./
	@echo "✓ Runtime library built: $(RUNTIME_LIB) + $(RUNTIME_SHARED)"
endif

# File targets funnel through the phony `runtime` target (order-only), so
# a parallel make never runs two `make -C runtime` invocations at once —
# the phony target and these file rules used to each spawn their own
# sub-make, and the two concurrent shared-library links raced on the same
# output file (surfaced on Windows as ld: "final link failed: file
# truncated"; Linux had the same race and just never happened to lose it)
$(RUNTIME_LIB): | runtime
	@:

$(RUNTIME_BUILD)/$(RUNTIME_LIB): | runtime
	@:

runtime-clean:
	$(MAKE) -C $(RUNTIME_DIR) clean
	rm -f $(RUNTIME_LIB) $(RUNTIME_SHARED)

compiler-clean:
	rm -f $(COMPILER_TARGET) $(COMPILER_OBJS)

# ========== WINDOWS (MinGW-w64) CROSS-COMPILATION ==========
# Cross-compile hemlock.exe, hemlockc.exe, and libhemlock_runtime.a from a
# POSIX host. Requirements (Debian/Ubuntu):
#   apt-get install gcc-mingw-w64-x86-64 libz-mingw-w64-dev
# The POSIX-threads flavor of MinGW-w64 (winpthreads) is required for the
# async runtime. Hash builtins use Windows CNG (bcrypt.dll); FFI (without
# libffi) and ECDSA are stubbed out (HEMLOCK_NO_FFI / HEMLOCK_NO_OPENSSL);
# see docs/advanced/windows.md.
# Native builds on MSYS2/MinGW are auto-detected via uname instead.
MINGW_CC ?= x86_64-w64-mingw32-gcc-posix

.PHONY: mingw mingw-interpreter mingw-clean
mingw:
	$(MAKE) CC=$(MINGW_CC) all

mingw-interpreter:
	$(MAKE) CC=$(MINGW_CC) hemlock.exe

# Removes only cross-build artifacts; native build products are untouched
mingw-clean:
	rm -rf build-mingw $(RUNTIME_DIR)/build-mingw hemlock.exe hemlockc.exe

# ========== WASM TARGET (Emscripten) ==========

WASM_RUNTIME_LIB = libhemlock_runtime_wasm.a
WASM_THREAD_RUNTIME_LIB = libhemlock_runtime_wasm_threaded.a

.PHONY: runtime-wasm runtime-wasm-threaded runtime-wasm-clean wasm-compile wasm-compile-threaded wasm-test

# Build WASM runtime library (non-threaded)
runtime-wasm:
	@echo "Building Hemlock WASM runtime library..."
	$(MAKE) -C $(RUNTIME_DIR) wasm
	cp $(RUNTIME_DIR)/build/$(WASM_RUNTIME_LIB) ./
	@echo "✓ WASM runtime library built: $(WASM_RUNTIME_LIB)"

# Build WASM threaded runtime library (pthreads via Web Workers)
runtime-wasm-threaded:
	@echo "Building Hemlock WASM threaded runtime library..."
	$(MAKE) -C $(RUNTIME_DIR) wasm-threaded
	cp $(RUNTIME_DIR)/build/$(WASM_THREAD_RUNTIME_LIB) ./
	@echo "✓ WASM threaded runtime library built: $(WASM_THREAD_RUNTIME_LIB)"

runtime-wasm-clean:
	rm -f $(WASM_RUNTIME_LIB) $(WASM_THREAD_RUNTIME_LIB)
	rm -f $(RUNTIME_DIR)/build/wasm_*.o $(RUNTIME_DIR)/build/$(WASM_RUNTIME_LIB)
	rm -f $(RUNTIME_DIR)/build/wasm_thread_*.o $(RUNTIME_DIR)/build/$(WASM_THREAD_RUNTIME_LIB)

# Compile a Hemlock program to WASM (non-threaded)
# Usage: make wasm-compile FILE=program.hml [OUT=program]
wasm-compile: compiler runtime-wasm
	@if [ -z "$(FILE)" ]; then \
		echo "Usage: make wasm-compile FILE=program.hml [OUT=program]"; \
		exit 1; \
	fi
	@OUT_NAME=$${OUT:-$$(basename $(FILE) .hml)}; \
	echo "Compiling $(FILE) to WASM..."; \
	./hemlockc --target wasm -o $$OUT_NAME $(FILE); \
	echo "✓ Built: $${OUT_NAME}.js + $${OUT_NAME}.wasm"

# Compile a Hemlock program to WASM with threading support
# Usage: make wasm-compile-threaded FILE=program.hml [OUT=program]
wasm-compile-threaded: compiler runtime-wasm-threaded
	@if [ -z "$(FILE)" ]; then \
		echo "Usage: make wasm-compile-threaded FILE=program.hml [OUT=program]"; \
		exit 1; \
	fi
	@OUT_NAME=$${OUT:-$$(basename $(FILE) .hml)}; \
	echo "Compiling $(FILE) to threaded WASM..."; \
	./hemlockc --target wasm --threads -o $$OUT_NAME $(FILE); \
	echo "✓ Built: $${OUT_NAME}.js + $${OUT_NAME}.wasm + $${OUT_NAME}.worker.js"

# Run WASM tests using Node.js
wasm-test: compiler runtime-wasm
	@echo "Running WASM tests..."
	@if [ -f tests/wasm/run_wasm_tests.sh ]; then \
		bash tests/wasm/run_wasm_tests.sh; \
	else \
		echo "No WASM tests found (tests/wasm/run_wasm_tests.sh)"; \
	fi

# Verify WASM test files produce correct output via native compiler
# (useful for CI where Emscripten may not be installed)
wasm-test-native: compiler
	@echo "Verifying WASM test files via native compiler..."
	@PASSED=0; FAILED=0; TOTAL=0; \
	for f in tests/wasm/*.hml; do \
		name=$$(basename "$$f" .hml); \
		expected="tests/wasm/$${name}.expected"; \
		if [ -f "$$expected" ]; then \
			TOTAL=$$((TOTAL + 1)); \
			if ./hemlockc -o /tmp/hml_wasm_test_$$name "$$f" 2>/dev/null; then \
				actual=$$(/tmp/hml_wasm_test_$$name 2>&1); \
				exp=$$(cat "$$expected"); \
				if [ "$$actual" = "$$exp" ]; then \
					echo "  ✓ $$name"; \
					PASSED=$$((PASSED + 1)); \
				else \
					echo "  ✗ $$name (output mismatch)"; \
					FAILED=$$((FAILED + 1)); \
				fi; \
				rm -f /tmp/hml_wasm_test_$$name; \
			else \
				echo "  ✗ $$name (compile error)"; \
				FAILED=$$((FAILED + 1)); \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "Results: $$PASSED passed, $$FAILED failed ($$TOTAL total)"; \
	if [ $$FAILED -gt 0 ]; then exit 1; fi

# ========== WASM INTERPRETER (Emscripten) ==========
# Compiles the entire tree-walking interpreter to WASM, allowing Hemlock
# source code to be parsed and executed directly in the browser.
# This is different from hemlockc's --target wasm, which compiles Hemlock
# programs to C then to WASM. This target compiles the interpreter itself.
#
# Requires: Emscripten SDK (emcc, emar)
# Usage: make wasm-interpreter
# Output: wasm/hemlock.js + wasm/hemlock.wasm

WASM_CC_INTERP = emcc
WASM_CFLAGS_INTERP = -Wall -Wextra -std=c11 -O2 -g \
	-D__EMSCRIPTEN__ -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_GNU_SOURCE \
	-Iinclude -Isrc -Isrc/frontend -Isrc/backends -Isrc/shared \
	-sUSE_ZLIB=1

# Interpreter WASM sources: shared frontend + interpreter backend (minus native-only files)
WASM_INTERP_FRONTEND_SRCS = $(FRONTEND_SRCS) $(MODULES_SRCS) $(SHARED_SRCS)

# Interpreter sources: exclude native main.c (wasm_interp_main.c and wasm_interp_shim.c
# are already included by the wildcard in INTERP_SRCS and have #ifdef __EMSCRIPTEN__ guards)
WASM_INTERP_BACKEND_SRCS = $(filter-out $(SRC_DIR)/backends/interpreter/main.c, $(INTERP_SRCS))

WASM_INTERP_ALL_SRCS = $(WASM_INTERP_FRONTEND_SRCS) $(WASM_INTERP_BACKEND_SRCS) $(TYPECHECK_SRCS)

# Emscripten linker flags for the interpreter
WASM_INTERP_LDFLAGS = \
	-sWASM=1 \
	-sALLOW_MEMORY_GROWTH=1 \
	-sSTACK_SIZE=1048576 \
	-sUSE_ZLIB=1 \
	-sEXPORTED_FUNCTIONS='["_main","_hemlock_eval","_hemlock_version","_hemlock_context_create","_hemlock_context_eval","_hemlock_context_destroy","_hemlock_context_get","_hemlock_context_set","_hemlock_context_last_error","_hemlock_compile_script","_hemlock_run_script","_hemlock_free_script","_malloc","_free"]' \
	-sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
	-sFORCE_FILESYSTEM=1 \
	-sMODULARIZE=0 \
	--pre-js wasm/pre.js \
	-lm

# Build directories for WASM interpreter objects
WASM_INTERP_BUILD_DIR = $(BUILD_DIR)/wasm_interp

.PHONY: wasm-interpreter wasm-interpreter-clean

wasm-interpreter:
	@echo "=== Building Hemlock WASM Interpreter ==="
	@echo "Compiling interpreter to WebAssembly via Emscripten..."
	@mkdir -p wasm $(WASM_INTERP_BUILD_DIR)
	@# Create pre.js if it doesn't exist
	@if [ ! -f wasm/pre.js ]; then echo "// Hemlock WASM pre-init" > wasm/pre.js; fi
	$(WASM_CC_INTERP) $(WASM_CFLAGS_INTERP) \
		$(WASM_INTERP_ALL_SRCS) \
		$(WASM_INTERP_LDFLAGS) \
		-o wasm/hemlock.js
	@echo ""
	@echo "✓ WASM interpreter built successfully!"
	@echo "  wasm/hemlock.js   - JavaScript loader/glue"
	@echo "  wasm/hemlock.wasm - WebAssembly binary"
	@echo ""
	@echo "Run with Node.js:  node -- wasm/hemlock.js -e 'print(\"Hello from WASM!\");'"
	@echo "Run in browser:    Open wasm/playground.html"
	@ls -lh wasm/hemlock.js wasm/hemlock.wasm 2>/dev/null || true

wasm-interpreter-clean:
	rm -f wasm/hemlock.js wasm/hemlock.wasm wasm/hemlock.worker.js
	rm -rf $(WASM_INTERP_BUILD_DIR)

# Serve the WASM browser example with a local HTTP server
# Builds the WASM interpreter if needed, then starts a dev server.
# Usage: make wasm-browser-example [PORT=8080]
.PHONY: wasm-browser-example
wasm-browser-example: wasm-interpreter
	@bash examples/wasm-browser/serve.sh $(or $(PORT),8080)

# Full clean including compiler, runtime, release, and static builds
fullclean: clean compiler-clean runtime-clean release-clean release-static-clean analyze-clean wasm-interpreter-clean

# Run compiler test suite
.PHONY: test-compiler
test-compiler: compiler
	@bash tests/compiler/run_compiler_tests.sh
	@bash tests/compiler/annotations/loop_pragma_codegen.sh

# Run borrow / ownership checker diagnostic tests
.PHONY: test-borrow
test-borrow: compiler
	@bash tests/borrow/run_borrow_tests.sh

# Run static lint / diagnostics tests
.PHONY: test-lint
test-lint: compiler
	@bash tests/lint/run_lint_tests.sh

# Check that interpreter tests compile (does not check output parity)
.PHONY: compile-check
compile-check: compiler
	@bash tests/run_compile_check.sh

# Run parity test suite (tests that must pass on both interpreter and compiler)
.PHONY: parity
parity: $(TARGET) compiler stdlib
	@bash tests/parity/run_parity_tests.sh

# Run full parity test (all interpreter tests through compiler)
.PHONY: parity-full
parity-full: $(TARGET) compiler stdlib
	@bash tests/run_full_parity.sh

# Run contract test suite (documented behavior pinned on both backends)
.PHONY: test-contracts
test-contracts: $(TARGET) compiler stdlib
	@bash tests/contracts/run_contract_tests.sh

# Run bundler test suite
.PHONY: test-bundler
test-bundler: $(TARGET)
	@bash tests/bundler/run_bundler_tests.sh

# Run LSP test suite
.PHONY: test-lsp
test-lsp: $(TARGET)
	@python3 tests/lsp/test_lsp.py

# Run compiler memory regression tests
.PHONY: test-memory
test-memory: compiler
	@bash tests/compiler/memory/run_memory_tests.sh

# Run all test suites
.PHONY: test-all
test-all: test test-compiler test-borrow test-lint parity test-contracts test-bundler test-lsp test-memory test-formatter test-cli test-check docs-check

# ========== RELEASE BUILD ==========

# Release flags: optimize for performance, no debug symbols
ifeq ($(shell uname),Darwin)
    RELEASE_CFLAGS = -Wall -Wextra -std=c11 -fwrapv -O3 -MMD -MP -D_DARWIN_C_SOURCE -Iinclude -Isrc -Isrc/frontend -Isrc/backends -Isrc/shared
else
    RELEASE_CFLAGS = -Wall -Wextra -std=c11 -fwrapv -O3 -MMD -MP -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinclude -Isrc -Isrc/frontend -Isrc/backends -Isrc/shared
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
                     $(RELEASE_BUILD_DIR)/frontend/lexer \
                     $(RELEASE_BUILD_DIR)/frontend/parser \
                     $(RELEASE_BUILD_DIR)/frontend/resolver \
                     $(RELEASE_BUILD_DIR)/frontend/optimizer \
                     $(RELEASE_BUILD_DIR)/backends/interpreter \
                     $(RELEASE_BUILD_DIR)/backends/interpreter/builtins \
                     $(RELEASE_BUILD_DIR)/backends/interpreter/io \
                     $(RELEASE_BUILD_DIR)/backends/interpreter/runtime \
                     $(RELEASE_BUILD_DIR)/backends/interpreter/profiler \
                     $(RELEASE_BUILD_DIR)/tools \
                     $(RELEASE_BUILD_DIR)/tools/lsp \
                     $(RELEASE_BUILD_DIR)/tools/bundler \
                     $(RELEASE_BUILD_DIR)/tools/formatter

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

$(RELEASE_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RELEASE_CFLAGS) -c $< -o $@

release-clean:
	rm -rf $(RELEASE_BUILD_DIR)

# ========== STATIC RELEASE BUILD ==========
# Build binaries with third-party libs statically linked for portability
# On Linux: static libffi/libz/libssl/libcrypto, dynamic glibc (for dlopen/FFI support)
# On macOS: static third-party libs, dynamic system frameworks

STATIC_BUILD_DIR = build-static

# Static library flags - prefer .a files over .so for third-party libs
ifeq ($(shell uname),Darwin)
    # macOS: Can't fully static link, but we can statically link third-party libs
    # System frameworks (libSystem) are always dynamic on macOS
    STATIC_LDFLAGS = $(LDFLAGS_LIBFFI) $(LDFLAGS_OPENSSL)
    ifneq ($(BREW_LIBFFI),)
        STATIC_LDFLAGS += $(BREW_LIBFFI)/lib/libffi.a
    else
        STATIC_LDFLAGS += -lffi
    endif
    # libssl must come before libcrypto (ssl depends on crypto)
    ifneq ($(BREW_OPENSSL),)
        STATIC_LDFLAGS += $(BREW_OPENSSL)/lib/libssl.a $(BREW_OPENSSL)/lib/libcrypto.a
    else
        STATIC_LDFLAGS += -lssl -lcrypto
    endif
    STATIC_LDFLAGS += -lm -lpthread -lz
    ifeq ($(HAS_LIBWEBSOCKETS),1)
        ifneq ($(BREW_LIBWEBSOCKETS),)
            STATIC_LDFLAGS += $(BREW_LIBWEBSOCKETS)/lib/libwebsockets.a
        else
            STATIC_LDFLAGS += -lwebsockets
        endif
    endif
else
    # Linux: Hybrid static/dynamic linking
    # - Static: libffi, libz, libssl, libcrypto, libwebsockets
    # - Dynamic: glibc, libcap, libuv, libev (no static libs available on Ubuntu)
    # Requires: libffi-dev, zlib1g-dev, libssl-dev, libwebsockets-dev,
    #           libcap-dev, libuv1-dev, libev-dev
    # Note: libssl must come before libcrypto
    STATIC_LDFLAGS = -Wl,-Bstatic -lffi -lz -Wl,-Bdynamic -lm -lpthread -ldl
    ifeq ($(HAS_LIBWEBSOCKETS),1)
        # libwebsockets requires: libssl, libcrypto (static), libcap, libuv, libev (dynamic)
        STATIC_LDFLAGS += -Wl,-Bstatic -lwebsockets -lssl -lcrypto -Wl,-Bdynamic -lcap -luv -lev
    else
        STATIC_LDFLAGS += -Wl,-Bstatic -lssl -lcrypto -Wl,-Bdynamic
    endif
endif

# Compiler-only static flags (full static OK since no dlopen needed)
ifeq ($(shell uname),Darwin)
    STATIC_COMPILER_LDFLAGS = $(STATIC_LDFLAGS)
else
    # Linux: Compiler can be fully static (no FFI/dlopen needed)
    STATIC_COMPILER_LDFLAGS = -static -lm
endif

STATIC_OBJS = $(patsubst $(SRC_DIR)/%.c,$(STATIC_BUILD_DIR)/%.o,$(SRCS))

# Static build directories
STATIC_BUILD_DIRS = $(STATIC_BUILD_DIR) \
                    $(STATIC_BUILD_DIR)/frontend \
                    $(STATIC_BUILD_DIR)/frontend/lexer \
                    $(STATIC_BUILD_DIR)/frontend/parser \
                    $(STATIC_BUILD_DIR)/frontend/resolver \
                    $(STATIC_BUILD_DIR)/frontend/optimizer \
                    $(STATIC_BUILD_DIR)/backends/interpreter \
                    $(STATIC_BUILD_DIR)/backends/interpreter/builtins \
                    $(STATIC_BUILD_DIR)/backends/interpreter/io \
                    $(STATIC_BUILD_DIR)/backends/interpreter/runtime \
                    $(STATIC_BUILD_DIR)/backends/interpreter/profiler \
                    $(STATIC_BUILD_DIR)/backends/compiler \
                    $(STATIC_BUILD_DIR)/tools \
                    $(STATIC_BUILD_DIR)/tools/lsp \
                    $(STATIC_BUILD_DIR)/tools/bundler \
                    $(STATIC_BUILD_DIR)/tools/formatter

.PHONY: release-static release-static-compiler release-static-clean

# Build static, optimized, stripped binary for portable distribution
release-static: $(STATIC_BUILD_DIRS) $(STATIC_BUILD_DIR)/hemlock $(STATIC_BUILD_DIR)/hemlockc
	@echo ""
	@echo "✓ Static release build complete:"
	@ls -lh $(STATIC_BUILD_DIR)/hemlock $(STATIC_BUILD_DIR)/hemlockc
ifeq ($(shell uname),Linux)
	@echo ""
	@echo "Verifying static linking..."
	@file $(STATIC_BUILD_DIR)/hemlock
	@ldd $(STATIC_BUILD_DIR)/hemlock 2>&1 || echo "  (statically linked - no dynamic dependencies)"
endif

# Build only the static compiler (for release builds with dynamic interpreter)
release-static-compiler: $(STATIC_BUILD_DIRS) $(STATIC_BUILD_DIR)/hemlockc
	@echo ""
	@echo "✓ Static compiler build complete:"
	@ls -lh $(STATIC_BUILD_DIR)/hemlockc
ifeq ($(shell uname),Linux)
	@echo ""
	@echo "Verifying static linking..."
	@file $(STATIC_BUILD_DIR)/hemlockc
	@ldd $(STATIC_BUILD_DIR)/hemlockc 2>&1 || echo "  (statically linked - no dynamic dependencies)"
endif

$(STATIC_BUILD_DIRS):
	mkdir -p $@

# Static interpreter build
$(STATIC_BUILD_DIR)/hemlock: $(STATIC_OBJS)
	$(CC) $(STATIC_OBJS) -o $@ $(STATIC_LDFLAGS)
	strip $@

# Static compiler build (needs runtime library first)
STATIC_COMPILER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(STATIC_BUILD_DIR)/%.o,$(COMPILER_SRCS))

$(STATIC_BUILD_DIR)/hemlockc: $(STATIC_COMPILER_OBJS) $(RUNTIME_LIB)
ifeq ($(shell uname),Darwin)
	$(CC) $(STATIC_COMPILER_OBJS) -o $@ -lm
else
	$(CC) -static $(STATIC_COMPILER_OBJS) -o $@ -lm
endif
	strip $@

$(STATIC_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RELEASE_CFLAGS) -c $< -o $@

release-static-clean:
	rm -rf $(STATIC_BUILD_DIR)

# ========== INSTALLATION ==========

# Installation directories (can be overridden)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/hemlock
DESTDIR ?=

.PHONY: install install-compiler uninstall

install: $(TARGET) $(COMPILER_TARGET) $(RUNTIME_LIB)
	@echo "Installing Hemlock to $(DESTDIR)$(PREFIX)..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Installing stdlib to $(DESTDIR)$(LIBDIR)..."
	install -d $(DESTDIR)$(LIBDIR)/stdlib
	cp -r stdlib/* $(DESTDIR)$(LIBDIR)/stdlib/
	@echo "Installing compiler to $(DESTDIR)$(BINDIR)..."
	install -m 755 $(COMPILER_TARGET) $(DESTDIR)$(BINDIR)/$(COMPILER_TARGET)
	@echo "Installing runtime library to $(DESTDIR)$(LIBDIR)..."
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(RUNTIME_LIB) $(DESTDIR)$(LIBDIR)/$(RUNTIME_LIB)
	@# Install runtime headers if available
	@if [ -d $(RUNTIME_DIR)/include ]; then \
		echo "Installing runtime headers to $(DESTDIR)$(LIBDIR)/include..."; \
		install -d $(DESTDIR)$(LIBDIR)/include; \
		cp -r $(RUNTIME_DIR)/include/* $(DESTDIR)$(LIBDIR)/include/; \
		cp include/hemlock_platform.h include/hemlock_compat.h $(DESTDIR)$(LIBDIR)/include/; \
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
		cp include/hemlock_platform.h include/hemlock_compat.h $(DESTDIR)$(LIBDIR)/include/; \
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

# ========== AUTO HEADER DEPS ==========
# Pull in the per-object .d files emitted by `gcc -MMD -MP`. The leading `-`
# on `include` makes a missing .d non-fatal (first build, fresh checkout, etc).
# Globbing across the build tree picks up debug, release, static, and runtime
# objects without having to enumerate the OBJS lists explicitly.
-include $(shell find $(BUILD_DIR) $(RELEASE_BUILD_DIR) $(STATIC_BUILD_DIR) -name '*.d' 2>/dev/null)

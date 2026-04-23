# Hemlock Fuzz Testing Makefile
#
# Supports multiple fuzzing frameworks:
#   - libFuzzer (LLVM) - fast, in-process fuzzing
#   - AFL++ - coverage-guided fuzzing
#   - Standalone - for manual crash reproduction
#
# Usage:
#   make fuzz-lexer-libfuzzer   - Build and run lexer fuzzer with libFuzzer
#   make fuzz-parser-libfuzzer  - Build and run parser fuzzer with libFuzzer
#   make fuzz-lexer-afl         - Build lexer fuzzer for AFL++
#   make fuzz-parser-afl        - Build parser fuzzer for AFL++
#   make standalone             - Build standalone harnesses for crash reproduction
#   make clean                  - Clean build artifacts

# Root directory
ROOT_DIR = ../..
SRC_DIR = $(ROOT_DIR)/src
INCLUDE_DIR = $(ROOT_DIR)/include

# Build directories
BUILD_DIR = build
CORPUS_DIR = corpus
CRASHES_DIR = crashes

# Source files for linking
FRONTEND_SRCS = $(SRC_DIR)/frontend/lexer.c \
                $(SRC_DIR)/frontend/ast.c \
                $(wildcard $(SRC_DIR)/frontend/parser/*.c)

# Common flags
COMMON_CFLAGS = -I$(INCLUDE_DIR) -I$(SRC_DIR) -I$(SRC_DIR)/frontend -std=c11 -g

# libFuzzer (requires clang)
LIBFUZZER_CC = clang
LIBFUZZER_CFLAGS = $(COMMON_CFLAGS) -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer

# AFL++ (requires afl-clang-fast or afl-clang-lto)
AFL_CC = afl-clang-fast
AFL_CFLAGS = $(COMMON_CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer

# Standalone (for crash reproduction)
STANDALONE_CC = gcc
STANDALONE_CFLAGS = $(COMMON_CFLAGS) -DFUZZ_STANDALONE -fsanitize=address,undefined -fno-omit-frame-pointer

# Detect available compilers
HAS_CLANG := $(shell command -v clang 2>/dev/null)
HAS_AFL := $(shell command -v afl-clang-fast 2>/dev/null)

.PHONY: all clean dirs libfuzzer afl standalone help

all: help

help:
	@echo "Hemlock Fuzz Testing"
	@echo ""
	@echo "Targets:"
	@echo "  make fuzz-lexer-libfuzzer   - Fuzz lexer with libFuzzer (requires clang)"
	@echo "  make fuzz-parser-libfuzzer  - Fuzz parser with libFuzzer (requires clang)"
	@echo "  make fuzz-lexer-afl         - Build lexer fuzzer for AFL++"
	@echo "  make fuzz-parser-afl        - Build parser fuzzer for AFL++"
	@echo "  make standalone             - Build standalone harnesses"
	@echo "  make clean                  - Remove build artifacts"
	@echo ""
	@echo "Running AFL++ (after building):"
	@echo "  mkdir -p $(CRASHES_DIR)"
	@echo "  afl-fuzz -i $(CORPUS_DIR) -o $(CRASHES_DIR) -- ./$(BUILD_DIR)/fuzz_lexer_afl @@"
	@echo ""
	@echo "Reproducing crashes:"
	@echo "  ./$(BUILD_DIR)/fuzz_lexer_standalone <crash_file>"

dirs:
	@mkdir -p $(BUILD_DIR) $(CRASHES_DIR)

# ========== LIBFUZZER TARGETS ==========

$(BUILD_DIR)/fuzz_lexer_libfuzzer: fuzz_lexer.c $(FRONTEND_SRCS) | dirs
ifndef HAS_CLANG
	@echo "Error: clang not found. libFuzzer requires clang."
	@exit 1
endif
	$(LIBFUZZER_CC) $(LIBFUZZER_CFLAGS) -o $@ $^

$(BUILD_DIR)/fuzz_parser_libfuzzer: fuzz_parser.c $(FRONTEND_SRCS) | dirs
ifndef HAS_CLANG
	@echo "Error: clang not found. libFuzzer requires clang."
	@exit 1
endif
	$(LIBFUZZER_CC) $(LIBFUZZER_CFLAGS) -o $@ $^

fuzz-lexer-libfuzzer: $(BUILD_DIR)/fuzz_lexer_libfuzzer
	@echo "Starting lexer fuzzing with libFuzzer..."
	@echo "Press Ctrl+C to stop"
	@mkdir -p $(CRASHES_DIR)/lexer
	./$(BUILD_DIR)/fuzz_lexer_libfuzzer $(CORPUS_DIR) -artifact_prefix=$(CRASHES_DIR)/lexer/

fuzz-parser-libfuzzer: $(BUILD_DIR)/fuzz_parser_libfuzzer
	@echo "Starting parser fuzzing with libFuzzer..."
	@echo "Press Ctrl+C to stop"
	@mkdir -p $(CRASHES_DIR)/parser
	./$(BUILD_DIR)/fuzz_parser_libfuzzer $(CORPUS_DIR) -artifact_prefix=$(CRASHES_DIR)/parser/

# ========== AFL++ TARGETS ==========

$(BUILD_DIR)/fuzz_lexer_afl: fuzz_lexer.c $(FRONTEND_SRCS) | dirs
ifndef HAS_AFL
	@echo "Error: afl-clang-fast not found. Install AFL++ first."
	@echo "  Ubuntu: sudo apt install afl++"
	@echo "  macOS:  brew install aflplusplus"
	@exit 1
endif
	AFL_HARDEN=1 $(AFL_CC) $(AFL_CFLAGS) -o $@ $^

$(BUILD_DIR)/fuzz_parser_afl: fuzz_parser.c $(FRONTEND_SRCS) | dirs
ifndef HAS_AFL
	@echo "Error: afl-clang-fast not found. Install AFL++ first."
	@echo "  Ubuntu: sudo apt install afl++"
	@echo "  macOS:  brew install aflplusplus"
	@exit 1
endif
	AFL_HARDEN=1 $(AFL_CC) $(AFL_CFLAGS) -o $@ $^

fuzz-lexer-afl: $(BUILD_DIR)/fuzz_lexer_afl
	@echo "Build complete. To run AFL++:"
	@echo "  afl-fuzz -i $(CORPUS_DIR) -o $(CRASHES_DIR)/lexer -- ./$(BUILD_DIR)/fuzz_lexer_afl @@"

fuzz-parser-afl: $(BUILD_DIR)/fuzz_parser_afl
	@echo "Build complete. To run AFL++:"
	@echo "  afl-fuzz -i $(CORPUS_DIR) -o $(CRASHES_DIR)/parser -- ./$(BUILD_DIR)/fuzz_parser_afl @@"

afl: fuzz-lexer-afl fuzz-parser-afl

# ========== STANDALONE TARGETS ==========

$(BUILD_DIR)/fuzz_lexer_standalone: fuzz_lexer.c $(FRONTEND_SRCS) | dirs
	$(STANDALONE_CC) $(STANDALONE_CFLAGS) -o $@ $^

$(BUILD_DIR)/fuzz_parser_standalone: fuzz_parser.c $(FRONTEND_SRCS) | dirs
	$(STANDALONE_CC) $(STANDALONE_CFLAGS) -o $@ $^

standalone: $(BUILD_DIR)/fuzz_lexer_standalone $(BUILD_DIR)/fuzz_parser_standalone
	@echo "Standalone harnesses built:"
	@echo "  $(BUILD_DIR)/fuzz_lexer_standalone"
	@echo "  $(BUILD_DIR)/fuzz_parser_standalone"
	@echo ""
	@echo "Usage: ./$(BUILD_DIR)/fuzz_lexer_standalone <input_file>"

# ========== CLEANUP ==========

clean:
	rm -rf $(BUILD_DIR)
	@echo "Build artifacts cleaned"

clean-crashes:
	rm -rf $(CRASHES_DIR)
	@echo "Crash artifacts cleaned"

clean-all: clean clean-crashes

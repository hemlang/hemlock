# Hemlock Bytecode VM Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -std=c11 -D_POSIX_C_SOURCE=200809L
CFLAGS += -I../../../include -I. -I../../frontend -I.. -I../../bundler

# Source files (VM-specific)
VM_SRCS = main.c \
          vm.c \
          chunk.c \
          compiler.c \
          instruction.c \
          debug.c

VM_OBJS = $(VM_SRCS:.c=.o)

# Shared frontend objects
FRONTEND_DIR = ../../frontend
FRONTEND_SRCS = $(FRONTEND_DIR)/lexer.c \
                $(FRONTEND_DIR)/parser/core.c \
                $(FRONTEND_DIR)/parser/expressions.c \
                $(FRONTEND_DIR)/parser/statements.c \
                $(FRONTEND_DIR)/ast.c \
                $(FRONTEND_DIR)/ast_serialize.c

# Bundler
BUNDLER_DIR = ../../bundler
BUNDLER_SRCS = $(BUNDLER_DIR)/bundler.c

# Interpreter objects we can reuse
INTERP_DIR = ../interpreter
SHARED_SRCS = $(INTERP_DIR)/resolver.c \
              $(INTERP_DIR)/optimizer.c

# Output binary
TARGET = hemlockvm

.PHONY: all clean test parity

all: $(TARGET)

# Build by compiling all sources directly
$(TARGET): $(VM_SRCS) $(FRONTEND_SRCS) $(BUNDLER_SRCS) $(SHARED_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm -ldl -lz

# Individual object compilation
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Run VM-specific tests
test: $(TARGET)
	@echo "Running bytecode VM tests..."
	@./$(TARGET) ../../../tests/parity/language/primitives.hml

# Quick test
quick: $(TARGET)
	@echo "Testing: print(42);"
	@echo 'print(42);' | ./$(TARGET)

# Run parity tests (compare output with interpreter)
parity: $(TARGET)
	@echo "Running parity tests..."
	@for f in ../../../tests/parity/language/*.hml; do \
		echo -n "Testing $$f... "; \
		expected=$${f%.hml}.expected; \
		if [ -f "$$expected" ]; then \
			output=$$(./$(TARGET) "$$f" 2>&1); \
			expected_content=$$(cat "$$expected"); \
			if [ "$$output" = "$$expected_content" ]; then \
				echo "PASS"; \
			else \
				echo "FAIL"; \
				echo "Expected:"; cat "$$expected"; \
				echo "Got:"; echo "$$output"; \
			fi \
		else \
			echo "SKIP (no expected file)"; \
		fi \
	done

# Disassemble a file (for debugging)
disasm: $(TARGET)
	./$(TARGET) --disasm $(FILE)

clean:
	rm -f $(VM_OBJS) $(TARGET)

# Dependencies
main.o: main.c vm.h chunk.h instruction.h compiler.h
vm.o: vm.c vm.h chunk.h instruction.h debug.h
chunk.o: chunk.c chunk.h instruction.h
compiler.o: compiler.c compiler.h chunk.h instruction.h
instruction.o: instruction.c instruction.h
debug.o: debug.c debug.h chunk.h instruction.h

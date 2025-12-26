# Hemlock Bytecode VM Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -std=c11
CFLAGS += -I../../../include -I.

# Source files
SRCS = main.c \
       vm.c \
       chunk.c \
       compiler.c \
       compile_expr.c \
       compile_stmt.c \
       compile_call.c \
       debug.c \
       builtins.c

OBJS = $(SRCS:.c=.o)

# Shared frontend objects
FRONTEND_OBJS = ../../frontend/lexer.o \
                ../../frontend/parser/parser.o \
                ../../frontend/parser/expressions.o \
                ../../frontend/parser/statements.o \
                ../../frontend/parser/patterns.o \
                ../../frontend/ast.o \
                ../../frontend/module.o

# Interpreter objects we can reuse (values, environment)
SHARED_OBJS = ../interpreter/values.o \
              ../interpreter/environment.o \
              ../interpreter/resolver.o

# Output binary
TARGET = hemlockvm

.PHONY: all clean test parity

all: $(TARGET)

$(TARGET): $(OBJS) $(FRONTEND_OBJS) $(SHARED_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm -ldl -lffi

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Run VM-specific tests
test: $(TARGET)
	@echo "Running bytecode VM tests..."
	@./scripts/test_vm.sh

# Run parity tests (compare output with interpreter)
parity: $(TARGET)
	@echo "Running parity tests..."
	@./scripts/test_vm_parity.sh

# Disassemble a file (for debugging)
disasm: $(TARGET)
	./$(TARGET) --disasm $(FILE)

clean:
	rm -f $(OBJS) $(TARGET)

# Dependencies
main.o: main.c vm.h chunk.h instruction.h
vm.o: vm.c vm.h chunk.h instruction.h
chunk.o: chunk.c chunk.h instruction.h
compiler.o: compiler.c compiler.h chunk.h instruction.h
compile_expr.o: compile_expr.c compiler.h chunk.h instruction.h
compile_stmt.o: compile_stmt.c compiler.h chunk.h instruction.h
compile_call.o: compile_call.c compiler.h chunk.h instruction.h
debug.o: debug.c debug.h chunk.h instruction.h
builtins.o: builtins.c vm.h instruction.h

# Hemlock Runtime Library Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -fPIC -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm -lpthread

SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Library targets
STATIC_LIB = libhemlock_runtime.a
SHARED_LIB = libhemlock_runtime.so

.PHONY: all clean static shared

all: $(BUILD_DIR) static shared

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Static library
static: $(BUILD_DIR)/$(STATIC_LIB)

$(BUILD_DIR)/$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^
	@echo "Built static library: $@"

# Shared library
shared: $(BUILD_DIR)/$(SHARED_LIB)

$(BUILD_DIR)/$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)
	@echo "Built shared library: $@"

# Install to parent directory for easier linking
install: static shared
	cp $(BUILD_DIR)/$(STATIC_LIB) ../
	cp $(BUILD_DIR)/$(SHARED_LIB) ../
	@echo "Installed libraries to parent directory"

clean:
	rm -rf $(BUILD_DIR)
	rm -f ../$(STATIC_LIB) ../$(SHARED_LIB)

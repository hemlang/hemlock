# Hemlock Runtime Library Makefile

CC = gcc
LDFLAGS = -lm -lpthread -lffi

# Platform-specific settings
ifeq ($(shell uname),Darwin)
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -fPIC -Iinclude -I../src/shared -D_DARWIN_C_SOURCE
    # Detect libffi (Homebrew on macOS puts it in non-standard locations)
    BREW_LIBFFI := $(shell brew --prefix libffi 2>/dev/null)
    ifneq ($(BREW_LIBFFI),)
        CFLAGS += -I$(BREW_LIBFFI)/include
        LDFLAGS += -L$(BREW_LIBFFI)/lib
    endif
    # Detect OpenSSL
    BREW_OPENSSL := $(shell brew --prefix openssl 2>/dev/null)
    ifneq ($(BREW_OPENSSL),)
        CFLAGS += -I$(BREW_OPENSSL)/include
        LDFLAGS += -L$(BREW_OPENSSL)/lib
    endif
else
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -fPIC -Iinclude -I../src/shared -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
    LIBFFI_CFLAGS := $(shell pkg-config --cflags libffi 2>/dev/null)
    LIBFFI_LIBS := $(shell pkg-config --libs libffi 2>/dev/null)
    ifneq ($(LIBFFI_CFLAGS),)
        CFLAGS += $(LIBFFI_CFLAGS)
        LDFLAGS += $(LIBFFI_LIBS)
    endif
endif

# OpenSSL is required for crypto builtins (SHA256, SHA512, MD5, ECDSA)
# On macOS, -L path is set above; on Linux, system paths suffice
LDFLAGS += -lssl -lcrypto

# Check if zlib is available (both header AND library must exist)
ZLIB_CHECK := $(shell echo 'int main(){return 0;}' | $(CC) -x c - -lz -o /dev/null 2>/dev/null && echo yes)
ifeq ($(ZLIB_CHECK),yes)
    CFLAGS += -DHML_HAVE_ZLIB
    LDFLAGS += -lz
endif

# Check if libwebsockets is available
LWS_CHECK := $(shell pkg-config --exists libwebsockets 2>/dev/null && echo yes)
ifeq ($(LWS_CHECK),yes)
    CFLAGS += -DHML_HAVE_LIBWEBSOCKETS $(shell pkg-config --cflags libwebsockets)
    LDFLAGS += $(shell pkg-config --libs libwebsockets)
else
    # Fallback: check if header exists directly
    LWS_HEADER_CHECK := $(shell test -f /usr/include/libwebsockets.h && echo yes)
    ifeq ($(LWS_HEADER_CHECK),yes)
        CFLAGS += -DHML_HAVE_LIBWEBSOCKETS
        LDFLAGS += -lwebsockets
    endif
endif

SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include
SHARED_DIR = ../src/shared

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
SHARED_SRCS = $(wildcard $(SHARED_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
SHARED_OBJS = $(patsubst $(SHARED_DIR)/%.c,$(BUILD_DIR)/shared_%.o,$(SHARED_SRCS))

# Library targets
STATIC_LIB = libhemlock_runtime.a
ifeq ($(shell uname),Darwin)
    SHARED_LIB = libhemlock_runtime.dylib
else
    SHARED_LIB = libhemlock_runtime.so
endif

# WASM build configuration (Emscripten)
WASM_CC = emcc
WASM_AR = emar
WASM_CFLAGS = -Wall -Wextra -std=c11 -O2 -fPIC -Iinclude -I../src/shared -D__EMSCRIPTEN__
WASM_SRCS = $(SRCS)
WASM_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/wasm_%.o,$(WASM_SRCS))
WASM_SHARED_OBJS = $(patsubst $(SHARED_DIR)/%.c,$(BUILD_DIR)/wasm_shared_%.o,$(SHARED_SRCS))
WASM_STATIC_LIB = libhemlock_runtime_wasm.a

.PHONY: all clean static shared wasm

all: $(BUILD_DIR) static shared

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile shared module objects
$(BUILD_DIR)/shared_%.o: $(SHARED_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Static library
static: $(BUILD_DIR)/$(STATIC_LIB)

$(BUILD_DIR)/$(STATIC_LIB): $(OBJS) $(SHARED_OBJS)
	ar rcs $@ $^
	@echo "Built static library: $@"

# Shared library
shared: $(BUILD_DIR)/$(SHARED_LIB)

$(BUILD_DIR)/$(SHARED_LIB): $(OBJS) $(SHARED_OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)
	@echo "Built shared library: $@"

# Install to parent directory for easier linking
install: static shared
	cp $(BUILD_DIR)/$(STATIC_LIB) ../
	cp $(BUILD_DIR)/$(SHARED_LIB) ../
	@echo "Installed libraries to parent directory"

# WASM static library via Emscripten
wasm: $(BUILD_DIR) $(BUILD_DIR)/$(WASM_STATIC_LIB)

$(BUILD_DIR)/wasm_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_shared_%.o: $(SHARED_DIR)/%.c | $(BUILD_DIR)
	$(WASM_CC) $(WASM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(WASM_STATIC_LIB): $(WASM_OBJS) $(WASM_SHARED_OBJS)
	$(WASM_AR) rcs $@ $^
	@echo "Built WASM runtime library: $@"

clean:
	rm -rf $(BUILD_DIR)
	rm -f ../$(STATIC_LIB) ../$(SHARED_LIB)

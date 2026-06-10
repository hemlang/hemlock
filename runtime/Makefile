# Hemlock Runtime Library Makefile

CC = gcc
LDFLAGS = -lm -lpthread -lffi

# Windows (MinGW-w64) detection — mirrors the top-level Makefile
ifneq (,$(findstring mingw,$(CC))$(findstring MINGW,$(shell uname))$(findstring MSYS,$(shell uname)))
    HEMLOCK_WINDOWS = 1
endif

# Platform-specific settings
ifeq ($(HEMLOCK_WINDOWS),1)
    # No -fPIC (PE is always position independent); FFI and OpenSSL builtins
    # are stubbed (HEMLOCK_NO_FFI / HEMLOCK_NO_OPENSSL); winpthreads supplies
    # pthreads; ws2_32 supplies sockets. -I../include for hemlock_platform.h.
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -MMD -MP -Iinclude -I../src/shared -I../include -D_WIN32_WINNT=0x0601 -D__USE_MINGW_ANSI_STDIO=1 -DHEMLOCK_NO_FFI -DHEMLOCK_NO_OPENSSL
    LDFLAGS = -lm -lpthread -lws2_32
else ifeq ($(shell uname),Darwin)
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -MMD -MP -fPIC -Iinclude -I../src/shared -I../include -D_DARWIN_C_SOURCE
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
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -MMD -MP -fPIC -Iinclude -I../src/shared -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
    LIBFFI_CFLAGS := $(shell pkg-config --cflags libffi 2>/dev/null)
    LIBFFI_LIBS := $(shell pkg-config --libs libffi 2>/dev/null)
    ifneq ($(LIBFFI_CFLAGS),)
        CFLAGS += $(LIBFFI_CFLAGS)
        LDFLAGS += $(LIBFFI_LIBS)
    endif
endif

# OpenSSL is required for crypto builtins (SHA256, SHA512, MD5, ECDSA)
# On macOS, -L path is set above; on Linux, system paths suffice
# (not on Windows, where the crypto builtins are stubbed)
ifneq ($(HEMLOCK_WINDOWS),1)
LDFLAGS += -lssl -lcrypto
endif

# Check if zlib is available (both header AND library must exist)
ZLIB_CHECK := $(shell echo 'int main(){return 0;}' | $(CC) -x c - -lz -o /dev/null 2>/dev/null && echo yes)
ifeq ($(ZLIB_CHECK),yes)
    CFLAGS += -DHML_HAVE_ZLIB
    LDFLAGS += -lz
endif

# Check if libwebsockets is available (host pkg-config is meaningless when
# cross-compiling for Windows, so skip the probe there)
ifeq ($(HEMLOCK_WINDOWS),1)
LWS_CHECK :=
else
LWS_CHECK := $(shell pkg-config --exists libwebsockets 2>/dev/null && echo yes)
endif
ifeq ($(LWS_CHECK),yes)
    CFLAGS += -DHML_HAVE_LIBWEBSOCKETS $(shell pkg-config --cflags libwebsockets)
    LDFLAGS += $(shell pkg-config --libs libwebsockets)
else
    # Fallback: check the usual prefixes when pkg-config is missing
    # (common on macOS without `brew install pkg-config`). Without
    # this, the runtime compiled HTTP streaming as stubs and the link
    # step then failed with "Undefined symbols: _hml_lws_http_*"
    # because the builtin wrappers reference real impls that weren't
    # compiled in.
    LWS_HEADER_CHECK := $(shell test -f /usr/include/libwebsockets.h \
                              -o -f /opt/homebrew/include/libwebsockets.h \
                              -o -f /usr/local/include/libwebsockets.h \
                              && echo yes)
    ifeq ($(LWS_HEADER_CHECK),yes)
        CFLAGS += -DHML_HAVE_LIBWEBSOCKETS
        # If the header lives under a brew prefix, add its include + lib
        # paths so the linker can actually find -lwebsockets.
        LWS_BREW_PFX := $(shell test -f /opt/homebrew/include/libwebsockets.h && echo /opt/homebrew)
        ifneq ($(LWS_BREW_PFX),)
            CFLAGS += -I$(LWS_BREW_PFX)/include
            LDFLAGS += -L$(LWS_BREW_PFX)/lib
        else
            LWS_LOCAL_PFX := $(shell test -f /usr/local/include/libwebsockets.h && echo /usr/local)
            ifneq ($(LWS_LOCAL_PFX),)
                CFLAGS += -I$(LWS_LOCAL_PFX)/include
                LDFLAGS += -L$(LWS_LOCAL_PFX)/lib
            endif
        endif
        LDFLAGS += -lwebsockets
    endif
endif

SRC_DIR = src
BUILD_DIR = build
# Cross-compiling for Windows from a POSIX host: keep PE objects separate
# (mirrors the top-level Makefile)
ifeq ($(HEMLOCK_WINDOWS),1)
ifeq (,$(findstring MINGW,$(shell uname))$(findstring MSYS,$(shell uname)))
    BUILD_DIR = build-mingw
endif
endif
INCLUDE_DIR = include
SHARED_DIR = ../src/shared

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
SHARED_SRCS = $(filter-out $(SHARED_DIR)/file_io.c, $(wildcard $(SHARED_DIR)/*.c))
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
SHARED_OBJS = $(patsubst $(SHARED_DIR)/%.c,$(BUILD_DIR)/shared_%.o,$(SHARED_SRCS))

# Library targets
STATIC_LIB = libhemlock_runtime.a
ifeq ($(HEMLOCK_WINDOWS),1)
    SHARED_LIB = libhemlock_runtime.dll
else ifeq ($(shell uname),Darwin)
    SHARED_LIB = libhemlock_runtime.dylib
else
    SHARED_LIB = libhemlock_runtime.so
endif

# WASM build configuration (Emscripten)
WASM_CC = emcc
WASM_AR = emar
WASM_CFLAGS = -Wall -Wextra -std=gnu11 -O2 -fPIC -Iinclude -I../src/shared -D__EMSCRIPTEN__ -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_GNU_SOURCE
WASM_SRCS = $(SRCS)
WASM_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/wasm_%.o,$(WASM_SRCS))
WASM_SHARED_OBJS = $(patsubst $(SHARED_DIR)/%.c,$(BUILD_DIR)/wasm_shared_%.o,$(SHARED_SRCS))
WASM_STATIC_LIB = libhemlock_runtime_wasm.a

.PHONY: all clean static shared wasm wasm-threaded

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

# WASM threaded build configuration (Emscripten with pthreads)
# Enables spawn/join/channels via Web Workers with SharedArrayBuffer
WASM_THREAD_CFLAGS = $(WASM_CFLAGS) -pthread
WASM_THREAD_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/wasm_thread_%.o,$(WASM_SRCS))
WASM_THREAD_SHARED_OBJS = $(patsubst $(SHARED_DIR)/%.c,$(BUILD_DIR)/wasm_thread_shared_%.o,$(SHARED_SRCS))
WASM_THREAD_LIB = libhemlock_runtime_wasm_threaded.a

wasm-threaded: $(BUILD_DIR) $(BUILD_DIR)/$(WASM_THREAD_LIB)

$(BUILD_DIR)/wasm_thread_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(WASM_CC) $(WASM_THREAD_CFLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_thread_shared_%.o: $(SHARED_DIR)/%.c | $(BUILD_DIR)
	$(WASM_CC) $(WASM_THREAD_CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(WASM_THREAD_LIB): $(WASM_THREAD_OBJS) $(WASM_THREAD_SHARED_OBJS)
	$(WASM_AR) rcs $@ $^
	@echo "Built WASM threaded runtime library: $@"

clean:
	rm -rf $(BUILD_DIR) build build-mingw
	rm -f ../$(STATIC_LIB) ../$(SHARED_LIB)

# Auto header deps (-MMD -MP). Leading `-` makes a missing .d non-fatal
# on first build. See top-level Makefile for context.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
